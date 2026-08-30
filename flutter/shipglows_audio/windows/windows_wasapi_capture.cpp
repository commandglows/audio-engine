#include "windows_wasapi_capture.h"

#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <span>
#include <vector>

#include "shipglows/audio/segmented_wav_store.hpp"
#include "shipglows/audio/pcm_analysis.hpp"
#include "shipglows/audio/recording_preflight.hpp"

namespace shipglows_audio {
namespace {

using Microsoft::WRL::ComPtr;
using shipglows::audio::AudioFormat;
using shipglows::audio::SampleFormat;

bool MapWaveFormat(const WAVEFORMATEX& wave, AudioFormat* output) {
  if (output == nullptr || wave.nChannels == 0 || wave.nSamplesPerSec == 0) {
    return false;
  }

  SampleFormat sample_format;
  if (wave.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wave.wBitsPerSample == 32) {
    sample_format = SampleFormat::float32;
  } else if (wave.wFormatTag == WAVE_FORMAT_PCM) {
    switch (wave.wBitsPerSample) {
      case 16:
        sample_format = SampleFormat::int16;
        break;
      case 24:
        sample_format = SampleFormat::int24;
        break;
      case 32:
        sample_format = SampleFormat::int32;
        break;
      default:
        return false;
    }
  } else if (wave.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
             wave.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const auto& extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(wave);
    if (IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
        wave.wBitsPerSample == 32) {
      sample_format = SampleFormat::float32;
    } else if (IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
      switch (wave.wBitsPerSample) {
        case 16:
          sample_format = SampleFormat::int16;
          break;
        case 24:
          sample_format = SampleFormat::int24;
          break;
        case 32:
          sample_format = SampleFormat::int32;
          break;
        default:
          return false;
      }
    } else {
      return false;
    }
  } else {
    return false;
  }

  *output = AudioFormat{
      .sample_rate = wave.nSamplesPerSec,
      .channel_count = wave.nChannels,
      .sample_format = sample_format,
  };
  return output->valid() && output->bytes_per_frame() == wave.nBlockAlign;
}

}  // namespace

WindowsWasapiCapture::WindowsWasapiCapture() = default;

WindowsWasapiCapture::~WindowsWasapiCapture() { static_cast<void>(Stop()); }

bool WindowsWasapiCapture::Start(
    const std::filesystem::path& session_directory) {
  if (capture_thread_.joinable() || session_.state() !=
                                        shipglows::audio::SessionState::idle) {
    return false;
  }

  const auto preflight = shipglows::audio::recording_preflight(session_directory);
  if (!preflight.ready) {
    SetError(preflight.error_code);
    return false;
  }

  {
    std::lock_guard lock(state_mutex_);
    initialization_finished_ = false;
    initialization_succeeded_ = false;
    error_code_.clear();
  }
  stop_requested_.store(false);
  capture_finished_.store(false);
  capture_thread_ =
      std::thread(&WindowsWasapiCapture::CaptureWorker, this, session_directory);

  std::unique_lock lock(state_mutex_);
  const auto initialized = initialization_condition_.wait_for(
      lock, std::chrono::seconds(10),
      [this] { return initialization_finished_; });
  if (!initialized || !initialization_succeeded_) {
    lock.unlock();
    stop_requested_.store(true);
    if (const auto event = wake_event_.load(); event != nullptr) {
      SetEvent(static_cast<HANDLE>(event));
    }
    if (capture_thread_.joinable()) {
      capture_thread_.join();
    }
    if (storage_thread_.joinable()) {
      storage_thread_.join();
    }
    return false;
  }
  return true;
}

WasapiCaptureStatus WindowsWasapiCapture::Stop() {
  stop_requested_.store(true);
  if (session_.state() == shipglows::audio::SessionState::recording) {
    static_cast<void>(session_.request_stop());
  }
  if (const auto event = wake_event_.load(); event != nullptr) {
    SetEvent(static_cast<HANDLE>(event));
  }
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }
  if (storage_thread_.joinable()) {
    storage_thread_.join();
  }
  return Status();
}

WasapiCaptureStatus WindowsWasapiCapture::Status() const {
  std::lock_guard lock(state_mutex_);
  return {
      .state = session_.state(),
      .format = format_,
      .metrics = session_.metrics(),
      .error_code = error_code_,
  };
}

void WindowsWasapiCapture::CaptureWorker(
    std::filesystem::path session_directory) {
  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_result)) {
    SetInitializationResult(false, "com_initialization_failed");
    return;
  }

  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> device;
  ComPtr<IAudioClient> audio_client;
  ComPtr<IAudioCaptureClient> capture_client;
  WAVEFORMATEX* mix_format = nullptr;
  HANDLE audio_event = nullptr;
  HANDLE mmcss_handle = nullptr;
  DWORD mmcss_task_index = 0;

  auto cleanup = [&] {
    if (audio_client) {
      audio_client->Stop();
    }
    if (mmcss_handle != nullptr) {
      AvRevertMmThreadCharacteristics(mmcss_handle);
    }
    if (audio_event != nullptr) {
      CloseHandle(audio_event);
    }
    wake_event_.store(nullptr);
    if (mix_format != nullptr) {
      CoTaskMemFree(mix_format);
    }
    capture_finished_.store(true);
    CoUninitialize();
  };

  HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                    CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (SUCCEEDED(result)) {
    result = enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &device);
  }
  if (SUCCEEDED(result)) {
    result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(audio_client.GetAddressOf()));
  }
  if (SUCCEEDED(result)) {
    result = audio_client->GetMixFormat(&mix_format);
  }
  AudioFormat native_format{};
  if (FAILED(result) || mix_format == nullptr ||
      !MapWaveFormat(*mix_format, &native_format)) {
    SetInitializationResult(false, "unsupported_device_format");
    cleanup();
    return;
  }

  result = audio_client->Initialize(
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, 0, 0,
      mix_format, nullptr);
  if (FAILED(result)) {
    SetInitializationResult(false, "wasapi_initialize_failed");
    cleanup();
    return;
  }

  UINT32 endpoint_buffer_frames = 0;
  result = audio_client->GetBufferSize(&endpoint_buffer_frames);
  if (SUCCEEDED(result)) {
    audio_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    result = audio_event == nullptr
                 ? HRESULT_FROM_WIN32(GetLastError())
                 : audio_client->SetEventHandle(audio_event);
  }
  if (SUCCEEDED(result)) {
    result = audio_client->GetService(IID_PPV_ARGS(&capture_client));
  }
  if (FAILED(result)) {
    SetInitializationResult(false, "wasapi_capture_service_failed");
    cleanup();
    return;
  }

  const auto ring_bytes = static_cast<std::size_t>(native_format.sample_rate) *
                          native_format.bytes_per_frame() * 10;
  ring_ = std::make_unique<
      shipglows::audio::SpscAudioRingBuffer<std::byte>>(ring_bytes);
  {
    std::lock_guard lock(state_mutex_);
    format_ = native_format;
  }
  if (!session_.prepare(native_format) || !session_.start()) {
    SetInitializationResult(false, "session_state_failed");
    cleanup();
    return;
  }
  timestamp_tracker_.reset(native_format.sample_rate, lifecycle_generation_);

  storage_thread_ =
      std::thread(&WindowsWasapiCapture::StorageWorker, this, session_directory);
  wake_event_.store(audio_event);
  result = audio_client->Start();
  if (FAILED(result)) {
    session_.fail();
    SetInitializationResult(false, "wasapi_start_failed");
    cleanup();
    return;
  }
  SetInitializationResult(true);

  mmcss_handle = AvSetMmThreadCharacteristics(L"Audio", &mmcss_task_index);
  std::vector<std::byte> silence(
      static_cast<std::size_t>(endpoint_buffer_frames) *
      native_format.bytes_per_frame());

  while (!stop_requested_.load(std::memory_order_relaxed)) {
    const auto wait_result = WaitForSingleObject(audio_event, 200);
    if (wait_result == WAIT_TIMEOUT) {
      continue;
    }
    if (wait_result != WAIT_OBJECT_0) {
      SetError("wasapi_event_wait_failed");
      session_.fail();
      break;
    }

    UINT32 packet_frames = 0;
    while (SUCCEEDED(capture_client->GetNextPacketSize(&packet_frames)) &&
           packet_frames > 0) {
      BYTE* data = nullptr;
      DWORD flags = 0;
      UINT64 device_position = 0;
      UINT64 capture_time = 0;
      result = capture_client->GetBuffer(&data, &packet_frames, &flags,
                                         &device_position, &capture_time);
      if (FAILED(result)) {
        SetError(result == AUDCLNT_E_DEVICE_INVALIDATED
                     ? "device_invalidated"
                     : "wasapi_get_buffer_failed");
        session_.fail();
        stop_requested_.store(true);
        break;
      }

      if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
        session_.count_discontinuity();
      }
      // WASAPI reports QPC position in 100 ns units. A zero position means the
      // endpoint did not provide a usable timestamp for this packet.
      const auto gap_frames = timestamp_tracker_.observe(
          capture_time > 0 ? capture_time * 100ULL : 0, packet_frames,
          lifecycle_generation_);
      if (gap_frames > 0) {
        session_.count_timestamp_gap_frames(gap_frames);
      }
      const auto packet_bytes = static_cast<std::size_t>(packet_frames) *
                                native_format.bytes_per_frame();
      const auto packet = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0
                              ? std::span<const std::byte>(silence.data(),
                                                           packet_bytes)
                              : std::span<const std::byte>(
                                    reinterpret_cast<const std::byte*>(data),
                                    packet_bytes);
      const auto stored_bytes = ring_->push(packet);
      session_.count_captured_frames(stored_bytes /
                                     native_format.bytes_per_frame());
      if (stored_bytes < packet_bytes) {
        session_.count_dropped_frames(
            (packet_bytes - stored_bytes) / native_format.bytes_per_frame());
      }
      capture_client->ReleaseBuffer(packet_frames);
    }
  }

  cleanup();
}

void WindowsWasapiCapture::StorageWorker(
    std::filesystem::path session_directory) {
  try {
    shipglows::audio::SegmentedWavStore store(
        std::move(session_directory), format_,
        static_cast<std::uint64_t>(format_.sample_rate) * 5);
    const auto frame_bytes = format_.bytes_per_frame();
    const auto chunk_bytes = (64 * 1024 / frame_bytes) * frame_bytes;
    std::vector<std::byte> chunk(chunk_bytes);
    while (!capture_finished_.load(std::memory_order_acquire) ||
           (ring_ != nullptr && ring_->available_to_read() > 0)) {
      const auto available = ring_ == nullptr ? 0 : ring_->pop(chunk);
      if (available == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      const auto block = std::span<const std::byte>(chunk.data(), available);
      session_.count_clipped_samples(
          shipglows::audio::count_clipped_samples(block, format_));
      const auto levels = shipglows::audio::analyze_levels(block, format_);
      session_.set_levels(levels.peak, levels.rms);
      store.append(block);
    }
    store.finalize();
    if (session_.state() == shipglows::audio::SessionState::stopping) {
      static_cast<void>(session_.finish());
    }
  } catch (...) {
    SetError("storage_write_failed");
    session_.fail();
    stop_requested_.store(true);
    if (const auto event = wake_event_.load(); event != nullptr) {
      SetEvent(static_cast<HANDLE>(event));
    }
  }
}

void WindowsWasapiCapture::SetInitializationResult(bool success,
                                                    std::string error_code) {
  {
    std::lock_guard lock(state_mutex_);
    initialization_finished_ = true;
    initialization_succeeded_ = success;
    if (!error_code.empty()) {
      error_code_ = std::move(error_code);
    }
  }
  initialization_condition_.notify_all();
}

void WindowsWasapiCapture::SetError(std::string error_code) {
  std::lock_guard lock(state_mutex_);
  error_code_ = std::move(error_code);
}

}  // namespace shipglows_audio
