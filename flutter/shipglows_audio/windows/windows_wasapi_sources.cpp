#include "windows_wasapi_capture.h"
#include "wasapi_stereo_timeline.h"
#include "wasapi_output_activity.h"
#include <Windows.h>
#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <audioclientactivationparams.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <span>
#include <vector>

namespace shipglows_audio {
using Microsoft::WRL::ComPtr;

bool SupportsProcessLoopback() {
  // RtlGetVersion avoids the application-manifest version virtualization.
  using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
  const auto module = GetModuleHandleW(L"ntdll.dll");
  const auto version_function = module ? reinterpret_cast<RtlGetVersionFunction>(
      GetProcAddress(module, "RtlGetVersion")) : nullptr;
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  return version_function && version_function(&version) == 0 &&
      version.dwMajorVersion >= 10 && SupportsProcessLoopbackBuild(version.dwBuildNumber);
}

namespace {
// The OS retains the agile callback until completion. It owns its event and
// activation parameters, so timeout cannot leave a dangling stack reference.
class ProcessActivation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
 public:
  ProcessActivation() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    params_.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params_.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    params_.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    variant_.vt = VT_BLOB;
    variant_.blob.cbSize = sizeof(params_);
    variant_.blob.pBlobData = reinterpret_cast<BYTE*>(&params_);
  }
  ~ProcessActivation() { if (event_) CloseHandle(event_); }
  STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
    ComPtr<IUnknown> activated;
    HRESULT activation = E_FAIL;
    result_ = operation->GetActivateResult(&activation, &activated);
    if (SUCCEEDED(result_)) result_ = activation;
    if (SUCCEEDED(result_)) result_ = activated.As(&client_);
    SetEvent(event_);
    return S_OK;
  }
  HRESULT Activate(ComPtr<IAudioClient>& client) {
    if (!event_) return HRESULT_FROM_WIN32(GetLastError());
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    const auto result = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient), &variant_, this, &operation);
    if (FAILED(result)) return result;
    const auto wait = WaitForSingleObject(event_, 10000);
    if (wait != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(
        wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    if (SUCCEEDED(result_)) client = client_;
    return result_;
  }
 private:
  HANDLE event_;
  HRESULT result_ = E_PENDING;
  AUDIOCLIENT_ACTIVATION_PARAMS params_{};
  PROPVARIANT variant_{};
  ComPtr<IAudioClient> client_;
};
}

bool WindowsWasapiCapture::SelectSources(bool microphone_enabled,
    std::wstring input, std::wstring output) {
  if (session_.state() != shipglows::audio::SessionState::idle ||
      capture_thread_.joinable()) return false;
  explicit_sources_ = true;
  microphone_enabled_ = microphone_enabled;
  selected_endpoint_id_ = std::move(input);
  output_endpoint_id_ = std::move(output);
  return true;
}

void WindowsWasapiCapture::SourcesWorker(std::filesystem::path directory) {
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
    SetInitializationResult(false, "com_initialization_failed");
    return;
  }
  struct Source {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    HANDLE ready_event = nullptr;
  };
  std::array<Source, 2> sources;
  ComPtr<IMMDeviceEnumerator> enumerator;
  auto finish = [&] {
    for (auto& source : sources) {
      if (source.client) source.client->Stop();
      source.capture.Reset(); source.client.Reset(); source.device.Reset();
      if (source.ready_event) { CloseHandle(source.ready_event); source.ready_event = nullptr; }
    }
    enumerator.Reset();
    capture_finished_.store(true);
    CoUninitialize();
  };
  auto fail_start = [&](const char* error) {
    SetInitializationResult(false, error); finish();
  };
  if ((!microphone_enabled_ && output_endpoint_id_.empty()) ||
      (microphone_enabled_ && selected_endpoint_id_.empty())) {
    fail_start("capture_source_required"); return;
  }
  const bool system_audio = output_endpoint_id_ == kSystemAudioEndpoint;
  if (system_audio && !SupportsProcessLoopback()) {
    fail_start("system_audio_unsupported"); return;
  }
  if ((microphone_enabled_ || !system_audio) &&
      FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
      CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    fail_start("wasapi_enumerator_failed"); return;
  }
  // Both clients request the same shared-mode format. Windows performs channel
  // conversion and sample-rate conversion; QPC packet timestamps align clocks.
  WAVEFORMATEX wave{};
  wave.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
  wave.nChannels = 2; wave.nSamplesPerSec = 48000; wave.wBitsPerSample = 32;
  wave.nBlockAlign = 8; wave.nAvgBytesPerSec = 384000;
  for (size_t index = 0; index < sources.size(); ++index) {
    if (index == 0 ? !microphone_enabled_ : output_endpoint_id_.empty()) continue;
    auto& source = sources[index];
    const auto& id = index == 0 ? selected_endpoint_id_ : output_endpoint_id_;
    if (index == 1 && system_audio) {
      auto activation = Microsoft::WRL::Make<ProcessActivation>();
      if (!activation || FAILED(activation->Activate(source.client))) {
        fail_start("system_audio_activation_failed"); return;
      }
    } else {
    DWORD state = 0;
    if (FAILED(enumerator->GetDevice(id.c_str(), &source.device)) ||
        FAILED(source.device->GetState(&state)) || !(state & DEVICE_STATE_ACTIVE)) {
      fail_start("capture_source_unavailable"); return;
    }
    ComPtr<IMMEndpoint> endpoint;
    EDataFlow flow;
    if (FAILED(source.device.As(&endpoint)) || FAILED(endpoint->GetDataFlow(&flow)) ||
        flow != (index == 0 ? eCapture : eRender)) {
      fail_start("capture_source_invalid"); return;
    }
    if (FAILED(source.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(source.client.GetAddressOf())))) {
      fail_start("wasapi_source_initialize_failed"); return;
    }
    }
    const bool process_source = index == 1 && system_audio;
    const DWORD stream_flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
        (process_source ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : AUDCLNT_STREAMFLAGS_NOPERSIST) |
        (index == 1 ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
    if (process_source) {
      source.ready_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (!source.ready_event) { fail_start("system_audio_event_failed"); return; }
    }
    if (FAILED(source.client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags,
        process_source ? 0 : 1000000, 0, &wave, nullptr)) ||
        (process_source && FAILED(source.client->SetEventHandle(source.ready_event))) ||
        FAILED(source.client->GetService(IID_PPV_ARGS(&source.capture)))) {
      fail_start("wasapi_source_initialize_failed"); return;
    }
  }
  {
    std::lock_guard lock(state_mutex_);
    format_ = {.sample_rate = 48000, .channel_count = 2,
               .sample_format = shipglows::audio::SampleFormat::float32};
    if (!output_endpoint_id_.empty()) {
      output_active_milliseconds_ = 0;
      output_silent_milliseconds_ = 0;
    }
  }
  ring_ = std::make_unique<shipglows::audio::SpscAudioRingBuffer<std::byte>>(3840000);
  if (!session_.prepare(format_) || !session_.start()) {
    fail_start("session_state_failed"); return;
  }
  LARGE_INTEGER frequency{}, origin{};
  QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&origin);
  const auto origin_100ns = static_cast<UINT64>(
      static_cast<long double>(origin.QuadPart) * 10000000 / frequency.QuadPart);
  for (auto& source : sources) {
    if (source.client && FAILED(source.client->Start())) {
      session_.fail(); fail_start("wasapi_source_start_failed"); return;
    }
  }
  storage_thread_ = std::thread(&WindowsWasapiCapture::StorageWorker, this, directory);
  SetInitializationResult(true);
  constexpr uint64_t capacity = 96000;
  std::vector<float> timeline(capacity * 2, 0);
  std::vector<float> output_timeline(capacity * 2, 0);
  OutputActivityCounter output_activity;
  uint64_t consumed = 0;
  const float gain = microphone_enabled_ && !output_endpoint_id_.empty() ? 0.5f : 1.f;
  auto now_frame = [&]() -> uint64_t {
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    return static_cast<uint64_t>((now.QuadPart - origin.QuadPart) * 48000 / frequency.QuadPart);
  };
  auto drain = [&](uint64_t until, bool discard) {
    std::array<float, 960> block{};
    while (consumed < until) {
      const auto frames = std::min<uint64_t>(480, until - consumed);
      for (uint64_t f = 0; f < frames; ++f) {
        const auto output_index = ((consumed + f) % capacity) * 2;
        if (!discard && !output_endpoint_id_.empty()) {
          output_activity.Observe(output_timeline[output_index], output_timeline[output_index + 1]);
        }
        output_timeline[output_index] = output_timeline[output_index + 1] = 0;
        for (uint64_t c = 0; c < 2; ++c) {
          auto& sample = timeline[((consumed + f) % capacity) * 2 + c];
          block[f * 2 + c] = std::clamp(sample, -1.f, 1.f); sample = 0;
        }
      }
      if (!discard && session_.state() != shipglows::audio::SessionState::failed) {
        const auto bytes = std::as_bytes(std::span(block.data(), static_cast<size_t>(frames * 2)));
        const auto stored = ring_->push(bytes);
        session_.count_captured_frames(stored / 8);
        session_.count_dropped_frames((bytes.size() - stored) / 8);
      }
      consumed += frames;
      if (!output_endpoint_id_.empty()) {
        std::lock_guard lock(state_mutex_);
        output_active_milliseconds_ = output_activity.active_milliseconds();
        output_silent_milliseconds_ = output_activity.silent_milliseconds();
      }
    }
  };
  bool failed = false;
  bool was_paused = false;
  while (!stop_requested_.load()) {
    if (suspend_requested_.exchange(false)) { failed = true; SetError("capture_system_interrupted"); break; }
    for (auto& source : sources) {
      if (!source.capture) continue;
      DWORD state = 0;
      if (source.device && (FAILED(source.device->GetState(&state)) || !(state & DEVICE_STATE_ACTIVE))) {
        failed = true; break;
      }
      UINT32 frames = 0;
      HRESULT result;
      while (SUCCEEDED(result = source.capture->GetNextPacketSize(&frames)) && frames) {
        BYTE* data = nullptr; DWORD flags = 0; UINT64 position = 0, qpc = 0;
        result = source.capture->GetBuffer(&data, &frames, &flags, &position, &qpc);
        if (FAILED(result)) break;
        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) session_.count_discontinuity();
        if (!qpc || (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) {
          source.capture->ReleaseBuffer(frames); failed = true; break;
        }
        session_.count_hardware_timestamp();
        const int64_t first = static_cast<int64_t>(qpc - origin_100ns) * 48 / 10000;
        // Drain the pending endpoint packet on the first pause observation:
        // it can contain the tail captured before the UI requested pause.
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) &&
            (!paused_.load() || !was_paused)) {
          const auto* samples = reinterpret_cast<const float*>(data);
          if (!AddStereoPacket(timeline, consumed, first,
              std::span(samples, static_cast<size_t>(frames) * 2), gain)) failed = true;
          if (&source == &sources[1] && !AddStereoPacket(output_timeline, consumed, first,
              std::span(samples, static_cast<size_t>(frames) * 2), 1.f)) failed = true;
        }
        source.capture->ReleaseBuffer(frames);
        if (failed) break;
      }
      if (FAILED(result)) failed = true;
      if (failed) break;
    }
    if (failed) { SetError("capture_source_lost"); break; }
    const auto now = now_frame();
    // A 100 ms jitter allowance keeps differently scheduled endpoint packets on
    // the same timeline. Silent loopback periods still produce elapsed silence.
    const bool is_paused = paused_.load();
    if (is_paused && !was_paused) drain(now, false);
    drain(is_paused ? now : (now > 4800 ? now - 4800 : 0), is_paused);
    was_paused = is_paused;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  // A storage failure has already terminated the consumer. Do not append more
  // bytes to its abandoned ring or claim that those frames were preserved.
  if (session_.state() != shipglows::audio::SessionState::failed) {
    const auto end = now_frame();
    const bool is_paused = paused_.load();
    // Stop can arrive before the capture thread observes a new user pause.
    // Preserve the already-buffered tail in that case, as in the normal loop.
    if (is_paused && !was_paused) drain(end, false);
    drain(end, is_paused);
  }
  if (failed) session_.fail();
  finish();
}
}  // namespace shipglows_audio
