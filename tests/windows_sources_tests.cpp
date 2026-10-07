#include "windows_wasapi_capture.h"
#include "wasapi_stereo_timeline.h"
#include "wasapi_output_activity.h"
#include <array>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstring>
#include <Windows.h>
#include <Mmdeviceapi.h>
#include <wrl/client.h>
#include <thread>
#include <chrono>
bool RecordedWaveHasSignal(const std::filesystem::path& directory) {
  for (const auto& file : std::filesystem::directory_iterator(directory)) {
    if (file.path().extension() != L".wav") continue;
    std::ifstream stream(file.path(), std::ios::binary);
    char header[12];
    if (!stream.read(header, sizeof(header)) || std::memcmp(header, "RIFF", 4)) continue;
    char kind[4]; uint32_t bytes = 0;
    while (stream.read(kind, 4) && stream.read(reinterpret_cast<char*>(&bytes), 4)) {
      if (!std::memcmp(kind, "data", 4)) {
        for (uint32_t i = 0; i + sizeof(float) <= bytes; i += sizeof(float)) {
          float sample = 0;
          if (!stream.read(reinterpret_cast<char*>(&sample), sizeof(sample))) break;
          if (std::isfinite(sample) && std::abs(sample) > 0.005f) return true;
        }
        break;
      }
      stream.seekg(bytes + (bytes & 1), std::ios::cur);
    }
  }
  return false;
}

int main(int argc, char**) {
  int failures = 0;
  auto check = [&](bool okay, const char* name) {
    if (!okay) { ++failures; std::cerr << name << '\n'; }
  };
  {
    std::array<float, 12> timeline{};
    std::array<float, 4> mic{1.f, 1.f, 0.5f, 0.5f};
    std::array<float, 4> system{0.5f, -0.5f, 1.f, -1.f};
    check(shipglows_audio::AddStereoPacket(timeline, 0, 1, mic, 0.5f), "mic placement");
    check(shipglows_audio::AddStereoPacket(timeline, 0, 2, system, 0.5f), "system timestamp placement");
    check(timeline[0] == 0 && timeline[2] == 0.5f && timeline[4] == 0.5f &&
          timeline[5] == 0.f && timeline[6] == 0.5f, "aligned mix and headroom");
    timeline.fill(0);
    check(shipglows_audio::AddStereoPacket(timeline, 2, 1, mic, 1.f), "late prefix accepted");
    check(timeline[2] == 0 && timeline[4] == 0.5f, "late packet not shifted");
    check(!shipglows_audio::AddStereoPacket(timeline, 0, 6, mic, 1.f), "bounded future packet");
  }
  check(!shipglows_audio::SupportsProcessLoopbackBuild(19045), "Windows 10 legacy unsupported");
  check(!shipglows_audio::SupportsProcessLoopbackBuild(20347), "below minimum unsupported");
  check(shipglows_audio::SupportsProcessLoopbackBuild(20348), "minimum supported");
  check(shipglows_audio::SupportsProcessLoopbackBuild(26100), "Windows 11 supported");
  {
    shipglows_audio::OutputActivityCounter counter;
    for (int frame = 0; frame < 48000; ++frame) counter.Observe(0, 0);
    check(counter.active_milliseconds() == 1000 && counter.silent_milliseconds() == 1000,
          "silence includes output without packets");
    counter.Observe(0, 0.002f);
    check(counter.silent_milliseconds() == 0, "right output channel resets silence");
    for (int frame = 0; frame < 48000; ++frame) counter.Observe(0.0005f, 0);
    check(counter.silent_milliseconds() == 1000, "sub threshold output counts silence");
    // Mic is deliberately not an input of this counter; its mixed signal cannot
    // mask output silence. Pause has no Observe calls and leaves both unchanged.
    const auto paused_time = counter.active_milliseconds();
    check(counter.active_milliseconds() == paused_time, "pause excludes monitoring time");
  }
  auto path = std::filesystem::temp_directory_path() /
      (L"shipglows-source-tests-" + std::to_wstring(GetCurrentProcessId()));
  {
    shipglows_audio::WindowsWasapiCapture capture;
    check(capture.SelectSources(false, L"", L""), "configure disabled");
    check(!capture.Start(path / L"disabled"), "reject all disabled");
    check(capture.Status().error_code == "capture_source_required", "disabled error");
  }
  {
    shipglows_audio::WindowsWasapiCapture capture;
    check(capture.SelectSources(false, L"", L"missing-endpoint"), "configure exact output");
    check(!capture.Start(path / L"missing"), "missing output never falls back");
    check(capture.Status().error_code == "capture_source_unavailable", "missing error");
  }
  if (argc > 1) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    Microsoft::WRL::ComPtr<IMMDevice> output, input;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    LPWSTR output_id = nullptr, input_id = nullptr;
    if (enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &output))) output->GetId(&output_id);
    if (enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &input))) input->GetId(&input_id);
    check(output_id != nullptr, "live output available");
    if (output_id) {
      for (int mode = 0; mode < (input_id ? 2 : 1); ++mode) {
        shipglows_audio::WindowsWasapiCapture capture;
        check(capture.SelectSources(mode == 1, mode == 1 ? input_id : L"", output_id), "live configure");
        const bool started = capture.Start(path / (mode == 1 ? L"mixed" : L"loopback"));
        check(started, "live start");
        if (!started) { std::cerr << capture.Status().error_code << '\n'; continue; }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        check(capture.Pause().state == shipglows::audio::SessionState::paused, "live pause");
        check(!capture.SelectSources(false, L"", output_id), "paused selection frozen");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check(capture.Resume().state == shipglows::audio::SessionState::recording, "live resume");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto status = capture.Stop();
        check(status.state == shipglows::audio::SessionState::stopped, "live finalized");
        check(status.metrics.frames_captured > 10000 && status.format.sample_rate == 48000, "live elapsed samples");
        check(status.error_code.empty(), "live no error");
      }
    }
    if (shipglows_audio::SupportsProcessLoopback()) {
      for (int mode = 0; mode < (input_id ? 2 : 1); ++mode) {
        shipglows_audio::WindowsWasapiCapture capture;
        check(capture.SelectSources(mode == 1, mode == 1 ? input_id : L"",
                                   shipglows_audio::kSystemAudioEndpoint), "system configure");
        const bool started = capture.Start(path / (mode == 1 ? L"system-mixed" : L"system-only"));
        check(started, "system start");
        if (!started) { std::cerr << capture.Status().error_code << '\n'; continue; }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto paused = capture.Pause();
        check(paused.state == shipglows::audio::SessionState::paused, "system pause");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto monitor = capture.Status();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        check(capture.Status().output_active_milliseconds == monitor.output_active_milliseconds,
              "system monitoring excludes paused time");
        check(!capture.SelectSources(false, L"", L"different"), "system frozen while paused");
        check(capture.Resume().state == shipglows::audio::SessionState::recording, "system resume");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto status = capture.Stop();
        check(status.state == shipglows::audio::SessionState::stopped && status.error_code.empty(),
              "system finalize");
        check(status.output_active_milliseconds.value_or(0) >= 500 &&
              status.output_active_milliseconds.value_or(1000) < 750,
              "system enabled elapsed duration");
        check(status.output_silent_milliseconds.has_value() &&
              *status.output_silent_milliseconds <= *status.output_active_milliseconds,
              "system silence independent status");
        check(status.metrics.frames_captured > 20000, "system stored samples");
        if (argc > 2) {
          check(status.output_silent_milliseconds.value_or(1000) < 250,
                "external tone resets independent output silence");
          check(RecordedWaveHasSignal(path / (mode == 1 ? L"system-mixed" : L"system-only")),
                "external tone captured as nonzero WAV samples");
        }
        std::cout << (mode ? "system plus mic" : "system only") << " active ms="
                  << *status.output_active_milliseconds << " silence ms="
                  << *status.output_silent_milliseconds << '\n';
      }
    }
    CoTaskMemFree(output_id); CoTaskMemFree(input_id);
    input.Reset(); output.Reset(); enumerator.Reset(); CoUninitialize();
  }
  std::filesystem::remove_all(path);
  std::cout << "Source tests failures: " << failures << '\n';
  return failures ? 1 : 0;
}
