#include "windows_wasapi_playback.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <wrl/client.h>

namespace shipglows_audio {
namespace {
using Microsoft::WRL::ComPtr;

WasapiPlaybackResult Failure(WasapiPlaybackFailure kind, HRESULT hr) noexcept {
  return {kind, static_cast<long>(hr)};
}

inline std::uint32_t BucketUpperBound(std::size_t bucket) noexcept {
  return bucket >= 31 ? 0xffffffffu : ((1u << (bucket + 1)) - 1u);
}
}  // namespace

WindowsWasapiPlayback::WindowsWasapiPlayback(WasapiRenderFunction render,
                                             void* context)
    : render_(render), context_(context) {
  for (auto& bucket : callback_time_buckets_) bucket.store(0, std::memory_order_relaxed);
  worker_ = std::thread(&WindowsWasapiPlayback::Worker, this);
}

WindowsWasapiPlayback::~WindowsWasapiPlayback() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
    command_ = Command::stop;
    ++request_id_;
  }
  command_cv_.notify_one();
  if (auto event = static_cast<HANDLE>(control_event_.load(std::memory_order_relaxed))) SetEvent(event);
  completion_cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

WasapiPlaybackResult WindowsWasapiPlayback::Start(const std::wstring& endpoint_id) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!render_) return Failure(WasapiPlaybackFailure::initialization_failed, E_POINTER);
  if (command_ != Command::none || (worker_.joinable() && completed_id_ != 0 &&
      result_.failure == WasapiPlaybackFailure::none && sample_rate_.load() != 0)) {
    return Failure(WasapiPlaybackFailure::already_running, E_UNEXPECTED);
  }
  if (worker_exited_) return result_.failure == WasapiPlaybackFailure::none
      ? Failure(WasapiPlaybackFailure::initialization_failed, E_UNEXPECTED) : result_;
  endpoint_id_ = endpoint_id;
  command_ = Command::start;
  const auto id = ++request_id_;
  command_cv_.notify_one();
  if (auto event = static_cast<HANDLE>(control_event_.load(std::memory_order_relaxed))) SetEvent(event);
  completion_cv_.wait(lock, [&] { return completed_id_ >= id || shutting_down_ || worker_exited_; });
  return result_;
}

WasapiPlaybackResult WindowsWasapiPlayback::Pause() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (command_ != Command::none) return Failure(WasapiPlaybackFailure::control_failed, E_PENDING);
  command_ = Command::pause;
  const auto id = ++request_id_;
  command_cv_.notify_one();
  if (auto event = static_cast<HANDLE>(control_event_.load(std::memory_order_relaxed))) SetEvent(event);
  completion_cv_.wait(lock, [&] { return completed_id_ >= id || shutting_down_ || worker_exited_; });
  return result_;
}

WasapiPlaybackResult WindowsWasapiPlayback::Resume() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (command_ != Command::none) return Failure(WasapiPlaybackFailure::control_failed, E_PENDING);
  command_ = Command::resume;
  const auto id = ++request_id_;
  command_cv_.notify_one();
  if (auto event = static_cast<HANDLE>(control_event_.load(std::memory_order_relaxed))) SetEvent(event);
  completion_cv_.wait(lock, [&] { return completed_id_ >= id || shutting_down_ || worker_exited_; });
  return result_;
}

WasapiPlaybackResult WindowsWasapiPlayback::Stop() {
  std::unique_lock<std::mutex> lock(mutex_);
  if (command_ != Command::none) return Failure(WasapiPlaybackFailure::control_failed, E_PENDING);
  command_ = Command::stop;
  const auto id = ++request_id_;
  command_cv_.notify_one();
  if (auto event = static_cast<HANDLE>(control_event_.load(std::memory_order_relaxed))) SetEvent(event);
  completion_cv_.wait(lock, [&] { return completed_id_ >= id || shutting_down_ || worker_exited_; });
  const auto result = result_;
  sample_rate_.store(0, std::memory_order_relaxed);
  return result;
}

WasapiPlaybackMetrics WindowsWasapiPlayback::Metrics() const noexcept {
  WasapiPlaybackMetrics m;
  m.callbacks = callbacks_.load(std::memory_order_relaxed);
  m.underruns = underruns_.load(std::memory_order_relaxed);
  m.last_callback_microseconds = last_callback_us_.load(std::memory_order_relaxed);
  m.max_callback_microseconds = max_callback_us_.load(std::memory_order_relaxed);
  const auto total = callbacks_.load(std::memory_order_relaxed);
  if (total != 0) {
    const auto target = (total * 99 + 99) / 100;
    std::uint64_t cumulative = 0;
    for (std::size_t i = 0; i < callback_time_buckets_.size(); ++i) {
      cumulative += callback_time_buckets_[i].load(std::memory_order_relaxed);
      if (cumulative >= target) {
        m.p99_callback_microseconds = BucketUpperBound(i);
        break;
      }
    }
  }
  m.sample_rate = sample_rate_.load(std::memory_order_relaxed);
  m.channels = channels_.load(std::memory_order_relaxed);
  m.buffer_frames = buffer_frames_.load(std::memory_order_relaxed);
  return m;
}

void WindowsWasapiPlayback::Worker() {
  const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_hr)) {
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = Failure(WasapiPlaybackFailure::initialization_failed, com_hr);
    completed_id_ = request_id_;
    worker_exited_ = true;
    completion_cv_.notify_all();
    return;
  }

  HANDLE control_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HANDLE audio_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!control_event || !audio_event) {
    const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
    if (control_event) CloseHandle(control_event);
    if (audio_event) CloseHandle(audio_event);
    CoUninitialize();
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = Failure(WasapiPlaybackFailure::initialization_failed, hr);
    completed_id_ = request_id_;
    worker_exited_ = true;
    completion_cv_.notify_all();
    return;
  }
  control_event_.store(control_event, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (command_ != Command::none || shutting_down_) SetEvent(control_event);
  }

  ComPtr<IAudioClient> client;
  ComPtr<IAudioRenderClient> render_client;
  WAVEFORMATEXTENSIBLE engine_format{};
  engine_format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  engine_format.Format.nChannels = 2;
  engine_format.Format.nSamplesPerSec = 48000;
  engine_format.Format.wBitsPerSample = 32;
  engine_format.Format.nBlockAlign = 2 * sizeof(float);
  engine_format.Format.nAvgBytesPerSec = 48000 * engine_format.Format.nBlockAlign;
  engine_format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  engine_format.Samples.wValidBitsPerSample = 32;
  engine_format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  engine_format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  std::vector<float> float_buffer;
  std::uint32_t channels = 0;
  bool active = false;
  bool paused = false;
  bool quit = false;

  while (!quit) {
    HANDLE waits[] = {control_event, audio_event};
    DWORD wait = active && !paused
                     ? WaitForMultipleObjects(2, waits, FALSE, INFINITE)
                     : WaitForSingleObject(control_event, INFINITE);
    if (active && !paused && wait == WAIT_OBJECT_0 + 1) {
      UINT32 padding = 0;
      HRESULT hr = client->GetCurrentPadding(&padding);
      UINT32 frames = 0;
      if (SUCCEEDED(hr)) {
        UINT32 total = 0;
        hr = client->GetBufferSize(&total);
        if (SUCCEEDED(hr)) frames = total > padding ? total - padding : 0;
      }
      if (FAILED(hr)) {
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = Failure(WasapiPlaybackFailure::stream_failed, hr);
        command_ = Command::stop;
        ++request_id_;
        SetEvent(control_event);
        continue;
      }
      if (frames == 0) continue;
      BYTE* bytes = nullptr;
      hr = render_client->GetBuffer(frames, &bytes);
      if (FAILED(hr)) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      const auto begin = std::chrono::steady_clock::now();
      const bool rendered = render_(float_buffer.data(), frames, channels, context_);
      const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - begin).count());
      last_callback_us_.store(elapsed, std::memory_order_relaxed);
      std::size_t bucket = 0;
      for (auto value = elapsed; value > 1 && bucket < 63; value >>= 1) ++bucket;
      callback_time_buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
      auto maximum = max_callback_us_.load(std::memory_order_relaxed);
      while (elapsed > maximum && !max_callback_us_.compare_exchange_weak(maximum, elapsed,
             std::memory_order_relaxed, std::memory_order_relaxed)) {}
      callbacks_.fetch_add(1, std::memory_order_relaxed);
      if (!rendered) underruns_.fetch_add(1, std::memory_order_relaxed);
      else std::memcpy(bytes, float_buffer.data(), static_cast<std::size_t>(frames) * channels * sizeof(float));
      hr = render_client->ReleaseBuffer(frames, rendered ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
      if (FAILED(hr)) underruns_.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    if (wait != WAIT_OBJECT_0) continue;

    Command command;
    std::wstring endpoint;
    std::uint64_t id;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      command = command_;
      endpoint = endpoint_id_;
      id = request_id_;
      command_ = Command::none;
      quit = shutting_down_;
    }
    if (quit || command == Command::stop) {
      if (client) client->Stop();
      active = false;
      paused = false;
      render_client.Reset();
      client.Reset();
      sample_rate_.store(0, std::memory_order_relaxed);
      if (quit) break;
    } else if (command == Command::start) {
      WasapiPlaybackResult result{};
      ComPtr<IMMDeviceEnumerator> enumerator;
      ComPtr<IMMDevice> device;
      HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&enumerator));
      if (SUCCEEDED(hr)) {
        hr = endpoint.empty() ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)
                              : enumerator->GetDevice(endpoint.c_str(), &device);
      }
      if (FAILED(hr)) result = Failure(endpoint.empty() ? WasapiPlaybackFailure::device_unavailable
                                                        : WasapiPlaybackFailure::invalid_endpoint, hr);
      if (result) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(client.GetAddressOf()));
      if (result && FAILED(hr)) result = Failure(WasapiPlaybackFailure::device_unavailable, hr);
      if (result) {
        channels = 2;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
              AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST |
                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
              0, 0, &engine_format.Format, nullptr);
      }
      if (result && FAILED(hr)) result = Failure(WasapiPlaybackFailure::initialization_failed, hr);
      if (result) hr = client->SetEventHandle(audio_event);
      if (result && FAILED(hr)) result = Failure(WasapiPlaybackFailure::initialization_failed, hr);
      if (result) hr = client->GetService(IID_PPV_ARGS(&render_client));
      if (result && FAILED(hr)) result = Failure(WasapiPlaybackFailure::initialization_failed, hr);
      UINT32 buffer_frames = 0;
      if (result) hr = client->GetBufferSize(&buffer_frames);
      if (result && FAILED(hr)) result = Failure(WasapiPlaybackFailure::initialization_failed, hr);
      if (result) {
        float_buffer.resize(static_cast<std::size_t>(buffer_frames) * channels);
        BYTE* initial = nullptr;
        hr = render_client->GetBuffer(buffer_frames, &initial);
        if (SUCCEEDED(hr)) hr = render_client->ReleaseBuffer(buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);
        if (FAILED(hr)) result = Failure(WasapiPlaybackFailure::initialization_failed, hr);
      }
      if (result) hr = client->Start();
      if (result && FAILED(hr)) result = Failure(WasapiPlaybackFailure::start_failed, hr);
      active = static_cast<bool>(result);
      paused = false;
      if (result) {
        sample_rate_.store(48000, std::memory_order_relaxed);
        channels_.store(channels, std::memory_order_relaxed);
        buffer_frames_.store(buffer_frames, std::memory_order_relaxed);
      } else {
        client.Reset(); render_client.Reset();
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = result;
        completed_id_ = id;
      }
      completion_cv_.notify_all();
    } else if (command == Command::pause || command == Command::resume) {
      HRESULT hr = E_UNEXPECTED;
      if (active && command == Command::pause) { hr = client->Stop(); paused = SUCCEEDED(hr); }
      else if (active && command == Command::resume && paused) { hr = client->Start(); paused = FAILED(hr); }
      WasapiPlaybackResult result = FAILED(hr) ? Failure(WasapiPlaybackFailure::control_failed, hr)
                                               : WasapiPlaybackResult{};
      { std::lock_guard<std::mutex> lock(mutex_); result_ = result; completed_id_ = id; }
      completion_cv_.notify_all();
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      result_ = Failure(WasapiPlaybackFailure::control_failed, E_UNEXPECTED);
      completed_id_ = id;
      completion_cv_.notify_all();
    }
  }
  if (client) client->Stop();
  CloseHandle(audio_event);
  CloseHandle(control_event);
  control_event_.store(nullptr, std::memory_order_relaxed);
  CoUninitialize();
}

}  // namespace shipglows_audio
