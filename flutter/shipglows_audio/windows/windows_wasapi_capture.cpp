#include "windows_wasapi_capture.h"

#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
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

WasapiPowerEvent ClassifyWasapiPowerBroadcast(
    std::uintptr_t event) noexcept {
  switch (event) {
    case PBT_APMSUSPEND:
      return WasapiPowerEvent::suspend;
    case PBT_APMRESUMEAUTOMATIC:
    case PBT_APMRESUMESUSPEND:
      return WasapiPowerEvent::resume;
    default:
      return WasapiPowerEvent::none;
  }
}

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
  paused_.store(false);
  route_change_requested_.store(WasapiRouteChange::none);
  {
    std::lock_guard endpoint_lock(endpoint_mutex_);
    selected_endpoint_id_.clear();
  }
  storage_command_.store(0);
  suspend_requested_.store(false);
  resume_requested_.store(false);
  system_suspended_.store(false);
  resume_to_user_pause_.store(false);
  resume_in_progress_.store(false);
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
    if (route_monitor_thread_.joinable()) {
      route_monitor_thread_.join();
    }
    return false;
  }
  return true;
}

WasapiCaptureStatus WindowsWasapiCapture::Stop() {
  stop_requested_.store(true);
  if (session_.state() == shipglows::audio::SessionState::recording ||
      session_.state() == shipglows::audio::SessionState::paused) {
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
  if (route_monitor_thread_.joinable()) {
    route_monitor_thread_.join();
  }
  return Status();
}

WasapiCaptureStatus WindowsWasapiCapture::Pause() {
  if (!session_.pause()) {
    return Status();
  }
  paused_.store(true, std::memory_order_release);
  if (!SubmitStorageCommand(1)) {
    SetError("pause_checkpoint_failed");
    session_.fail();
  }
  return Status();
}

WasapiCaptureStatus WindowsWasapiCapture::Resume() {
  if (session_.state() != shipglows::audio::SessionState::paused) {
    return Status();
  }
  if (!SubmitStorageCommand(2)) {
    SetError("resume_checkpoint_failed");
    session_.fail();
    return Status();
  }
  lifecycle_generation_.fetch_add(1, std::memory_order_relaxed);
  paused_.store(false, std::memory_order_release);
  static_cast<void>(session_.resume());
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

void WindowsWasapiCapture::NotifySystemSuspend() {
  const auto state = session_.state();
  if (state != shipglows::audio::SessionState::recording &&
      state != shipglows::audio::SessionState::paused) {
    return;
  }
  resume_to_user_pause_.store(
      state == shipglows::audio::SessionState::paused,
      std::memory_order_release);
  resume_in_progress_.store(false, std::memory_order_release);
  paused_.store(true, std::memory_order_release);
  suspend_requested_.store(true, std::memory_order_release);
  if (const auto event = wake_event_.load(); event != nullptr) {
    SetEvent(static_cast<HANDLE>(event));
  }
}

void WindowsWasapiCapture::NotifySystemResume() {
  const auto system_suspended =
      system_suspended_.load(std::memory_order_acquire);
  const auto suspend_requested =
      suspend_requested_.load(std::memory_order_acquire);
  const auto resume_in_progress =
      resume_in_progress_.load(std::memory_order_acquire);
  if (!ShouldQueueWasapiResume(system_suspended, suspend_requested,
                               resume_in_progress)) {
    return;
  }
  bool expected = false;
  if (!resume_in_progress_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return;
  }
  resume_requested_.store(true, std::memory_order_release);
  if (const auto event = wake_event_.load(); event != nullptr) {
    SetEvent(static_cast<HANDLE>(event));
  }
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

  auto close_stream = [&] {
    if (audio_client) {
      audio_client->Stop();
    }
    if (audio_event != nullptr) {
      CloseHandle(audio_event);
      audio_event = nullptr;
    }
    wake_event_.store(nullptr);
    if (mix_format != nullptr) {
      CoTaskMemFree(mix_format);
      mix_format = nullptr;
    }
    capture_client.Reset();
    audio_client.Reset();
    device.Reset();
  };
  auto cleanup = [&] {
    close_stream();
    if (mmcss_handle != nullptr) {
      AvRevertMmThreadCharacteristics(mmcss_handle);
    }
    capture_finished_.store(true);
    CoUninitialize();
  };

  HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                    CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (FAILED(result)) {
    SetInitializationResult(false, "wasapi_enumerator_failed");
    cleanup();
    return;
  }
  AudioFormat native_format{};
  UINT32 endpoint_buffer_frames = 0;
  auto open_selected_endpoint = [&](bool first_open) -> bool {
    close_stream();
    if (first_open) {
      result =
          enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &device);
    } else {
      std::wstring selected_id;
      {
        std::lock_guard endpoint_lock(endpoint_mutex_);
        selected_id = selected_endpoint_id_;
      }
      result = selected_id.empty()
                   ? E_NOTFOUND
                   : enumerator->GetDevice(selected_id.c_str(), &device);
    }
    if (FAILED(result)) return false;
    if (first_open) {
      LPWSTR selected_id = nullptr;
      if (FAILED(device->GetId(&selected_id)) || selected_id == nullptr) {
        return false;
      }
      {
        std::lock_guard endpoint_lock(endpoint_mutex_);
        selected_endpoint_id_ = selected_id;
      }
      CoTaskMemFree(selected_id);
    }
    result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(audio_client.GetAddressOf()));
    if (FAILED(result)) return false;
    result = audio_client->GetMixFormat(&mix_format);
    AudioFormat candidate{};
    if (FAILED(result) || mix_format == nullptr ||
        !MapWaveFormat(*mix_format, &candidate)) {
      return false;
    }
    if (first_open) {
      native_format = candidate;
    } else if (candidate.sample_rate != native_format.sample_rate ||
               candidate.channel_count != native_format.channel_count ||
               candidate.sample_format != native_format.sample_format) {
      CoTaskMemFree(mix_format);
      mix_format = static_cast<WAVEFORMATEX*>(
          CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
      if (mix_format == nullptr) return false;
      *mix_format = {};
      mix_format->wFormatTag = native_format.sample_format == SampleFormat::float32
                                  ? WAVE_FORMAT_IEEE_FLOAT
                                  : WAVE_FORMAT_PCM;
      mix_format->nChannels = native_format.channel_count;
      mix_format->nSamplesPerSec = native_format.sample_rate;
      mix_format->wBitsPerSample = native_format.bytes_per_sample() * 8;
      mix_format->nBlockAlign =
          static_cast<WORD>(native_format.bytes_per_frame());
      mix_format->nAvgBytesPerSec =
          native_format.sample_rate * native_format.bytes_per_frame();
    }
    result = audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        0, 0, mix_format, nullptr);
    if (FAILED(result)) return false;
    result = audio_client->GetBufferSize(&endpoint_buffer_frames);
    if (FAILED(result)) return false;
    audio_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (audio_event == nullptr ||
        FAILED(audio_client->SetEventHandle(audio_event))) {
      return false;
    }
    result = audio_client->GetService(IID_PPV_ARGS(&capture_client));
    if (FAILED(result)) return false;
    wake_event_.store(audio_event);
    return SUCCEEDED(audio_client->Start());
  };

  if (!open_selected_endpoint(true)) {
    SetInitializationResult(false, "wasapi_initialize_failed");
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
  timestamp_tracker_.reset(native_format.sample_rate,
                           lifecycle_generation_.load());

  storage_thread_ =
      std::thread(&WindowsWasapiCapture::StorageWorker, this, session_directory);
  route_monitor_thread_ =
      std::thread(&WindowsWasapiCapture::RouteMonitorWorker, this);
  SetInitializationResult(true);

  mmcss_handle = AvSetMmThreadCharacteristics(L"Audio", &mmcss_task_index);
  std::vector<std::byte> silence;
  auto resize_silence = [&] {
    silence.assign(static_cast<std::size_t>(endpoint_buffer_frames) *
                       native_format.bytes_per_frame(),
                   std::byte{});
  };
  resize_silence();
  bool reconnect_required = false;
  bool route_changed = false;
  bool system_resume = false;

  while (!stop_requested_.load(std::memory_order_relaxed)) {
    if (suspend_requested_.exchange(false, std::memory_order_acq_rel)) {
      session_.count_discontinuity();
      if (!SubmitStorageCommand(6)) {
        SetError("suspend_checkpoint_failed");
        session_.fail();
        break;
      }
      system_suspended_.store(true, std::memory_order_release);
    }
    if (resume_requested_.exchange(false, std::memory_order_acq_rel)) {
      reconnect_required = true;
      route_changed = false;
      system_resume = true;
      SetError("system_resumed");
    }
    if (system_suspended_.load(std::memory_order_acquire) && !system_resume) {
      WaitForSingleObject(audio_event, 200);
      continue;
    }
    if (reconnect_required) {
      if (!system_resume) {
        static_cast<void>(SubmitStorageCommand(route_changed ? 5 : 3));
      }
      close_stream();
      constexpr std::array delays{100, 200, 400, 800, 1600};
      bool reconnected = false;
      for (const auto delay_ms : delays) {
        if (stop_requested_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        if (open_selected_endpoint(false)) {
          reconnected = true;
          break;
        }
      }
      if (!reconnected) {
        resume_in_progress_.store(false, std::memory_order_release);
        SetError("device_reconnect_exhausted");
        session_.fail();
        break;
      }
      const auto generation =
          lifecycle_generation_.fetch_add(1, std::memory_order_relaxed) + 1;
      timestamp_tracker_.reset(native_format.sample_rate, generation);
      session_.count_device_restart();
      if (route_changed) session_.count_route_change();
      route_change_requested_.store(WasapiRouteChange::none,
                                    std::memory_order_release);
      SetError({});
      static_cast<void>(SubmitStorageCommand(system_resume ? 7 : 4));
      resize_silence();
      if (system_resume) {
        paused_.store(resume_to_user_pause_.load(std::memory_order_acquire),
                      std::memory_order_release);
        system_suspended_.store(false, std::memory_order_release);
        resume_in_progress_.store(false, std::memory_order_release);
      }
      reconnect_required = false;
      route_changed = false;
      system_resume = false;
      continue;
    }

    const auto wait_result = WaitForSingleObject(audio_event, 200);
    if (wait_result == WAIT_TIMEOUT) {
      continue;
    }
    if (wait_result != WAIT_OBJECT_0) {
      SetError("wasapi_event_wait_failed");
      reconnect_required = true;
      continue;
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
        reconnect_required = true;
        break;
      }

      if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) {
        session_.count_discontinuity();
      }
      // WASAPI reports QPC position in 100 ns units. A zero position means the
      // endpoint did not provide a usable timestamp for this packet.
      const auto gap_frames = timestamp_tracker_.observe(
          capture_time > 0 ? capture_time * 100ULL : 0, packet_frames,
          lifecycle_generation_.load(std::memory_order_relaxed));
      if (capture_time > 0) {
        session_.count_hardware_timestamp();
      } else {
        session_.count_timestamp_query_failure();
      }
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
      const auto stored_bytes = paused_.load(std::memory_order_acquire)
                                    ? packet_bytes
                                    : ring_->push(packet);
      if (paused_.load(std::memory_order_relaxed)) {
        capture_client->ReleaseBuffer(packet_frames);
        continue;
      }
      session_.count_captured_frames(stored_bytes /
                                     native_format.bytes_per_frame());
      if (stored_bytes < packet_bytes) {
        session_.count_dropped_frames(
            (packet_bytes - stored_bytes) / native_format.bytes_per_frame());
      }
      capture_client->ReleaseBuffer(packet_frames);
    }

    if (!reconnect_required) {
      const auto route_change = route_change_requested_.exchange(
          WasapiRouteChange::none, std::memory_order_acq_rel);
      if (route_change == WasapiRouteChange::none) continue;
      route_changed =
          route_change == WasapiRouteChange::default_device_changed;
      reconnect_required = true;
      SetError(route_changed ? "route_changed" : "device_invalidated");
    }
  }

  cleanup();
}

void WindowsWasapiCapture::RouteMonitorWorker() {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    while (!stop_requested_.load(std::memory_order_acquire) &&
           !capture_finished_.load(std::memory_order_acquire)) {
      ComPtr<IMMDevice> current;
      LPWSTR id = nullptr;
      if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
              eCapture, eMultimedia, &current)) &&
          SUCCEEDED(current->GetId(&id)) && id != nullptr) {
        const std::wstring current_id(id);
        CoTaskMemFree(id);
        std::wstring selected_id;
        {
          std::lock_guard endpoint_lock(endpoint_mutex_);
          selected_id = selected_endpoint_id_;
        }
        if (!selected_id.empty() && current_id != selected_id) {
          ComPtr<IMMDevice> selected;
          DWORD selected_state = 0;
          const bool selected_active =
              SUCCEEDED(enumerator->GetDevice(selected_id.c_str(), &selected)) &&
              SUCCEEDED(selected->GetState(&selected_state)) &&
              (selected_state & DEVICE_STATE_ACTIVE) != 0;
          const auto route_change =
              ClassifyWasapiRouteChange(true, selected_active);
          if (route_change == WasapiRouteChange::default_device_changed) {
            std::lock_guard endpoint_lock(endpoint_mutex_);
            selected_endpoint_id_ = current_id;
          }
          route_change_requested_.store(route_change,
                                        std::memory_order_release);
          if (const auto event = wake_event_.load(); event != nullptr) {
            SetEvent(static_cast<HANDLE>(event));
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
  }
  CoUninitialize();
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
        const auto command = storage_command_.exchange(0);
        if (command == 1) {
          store.checkpoint(shipglows::audio::SessionEvent::pause, "user_pause");
        } else if (command == 2) {
          store.checkpoint(shipglows::audio::SessionEvent::resume, "user_resume");
        } else if (command == 3) {
          store.checkpoint(shipglows::audio::SessionEvent::interruption,
                           "device_invalidated");
        } else if (command == 4) {
          store.checkpoint(shipglows::audio::SessionEvent::device_restart,
                           "default_device");
        } else if (command == 5) {
          store.checkpoint(shipglows::audio::SessionEvent::route_change,
                           "default_device_changed");
        } else if (command == 6) {
          store.checkpoint(shipglows::audio::SessionEvent::interruption,
                           "system_suspended");
        } else if (command == 7) {
          store.checkpoint(shipglows::audio::SessionEvent::device_restart,
                           "system_resume");
        }
        if (command != 0) {
          {
            std::lock_guard lock(state_mutex_);
            ++storage_command_completed_;
          }
          storage_command_condition_.notify_all();
        }
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

bool WindowsWasapiCapture::SubmitStorageCommand(std::uint8_t command) {
  std::unique_lock lock(state_mutex_);
  const auto expected_completion = storage_command_completed_ + 1;
  storage_command_.store(command, std::memory_order_release);
  return storage_command_condition_.wait_for(
      lock, std::chrono::seconds(5), [this, expected_completion] {
        return storage_command_completed_ >= expected_completion ||
               session_.state() == shipglows::audio::SessionState::failed;
      }) && storage_command_completed_ >= expected_completion;
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
