// Measures synthesizer latency on the default Windows render endpoint.
//
// The probe drives the real WindowsOutputBus (shared WASAPI stream, synth,
// master limiter) and posts metronome clicks. Two independent measurements:
//   - clock estimate: post time -> IAudioClock presentation of the onset,
//     recorded by the engine itself;
//   - loopback: post time -> first loopback-captured frame above threshold.
//     Loopback taps the engine mix, so it excludes driver/DAC delay that the
//     clock estimate includes.
// With --with-playback a synthetic tone stays attached as the playback input
// for the whole run; the probe then counts loopback windows where the tone
// vanished while the synth started, played and stopped.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "windows_output_bus.h"

namespace {

using Microsoft::WRL::ComPtr;
using shipglows_audio::WindowsOutputBus;

std::uint64_t SteadyNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

// presented_ns: driver presentation time from the packet QPC stamp.
// mixed_ns: latest time the frame can have left the engine mix, derived from
// when the probe read it (1 ms polling makes this an upper bound).
struct Frame {
  std::uint64_t presented_ns;
  std::uint64_t mixed_ns;
  float amplitude;
};

class LoopbackCapture {
 public:
  bool Start() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(client_.GetAddressOf()))) ||
        FAILED(client_->GetMixFormat(&format_))) return false;
    const bool is_float = format_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (format_->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format_)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    if (!is_float || format_->wBitsPerSample != 32) return false;
    if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                   200000, 0, format_, nullptr)) ||
        FAILED(client_->GetService(IID_PPV_ARGS(&capture_))) || FAILED(client_->Start())) return false;
    frames_.reserve(48000 * 30);
    running_ = true;
    thread_ = std::thread([this] { Loop(); });
    return true;
  }
  void Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
    if (format_) CoTaskMemFree(format_);
    format_ = nullptr;
  }
  [[nodiscard]] const std::vector<Frame>& frames() const { return frames_; }
  [[nodiscard]] std::uint32_t rate() const { return rate_; }
  struct Packet {
    std::uint64_t qpc_ns, read_ns;
    std::uint32_t frames;
    DWORD flags;
  };
  [[nodiscard]] const std::vector<Packet>& packets() const { return packets_; }

 private:
  void Loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    rate_ = format_->nSamplesPerSec;
    const auto channels = format_->nChannels;
    while (running_) {
      UINT32 packet = 0;
      while (SUCCEEDED(capture_->GetNextPacketSize(&packet)) && packet != 0) {
        BYTE* data = nullptr;
        UINT32 count = 0;
        DWORD flags = 0;
        UINT64 device_position = 0, qpc_100ns = 0;
        if (FAILED(capture_->GetBuffer(&data, &count, &flags, &device_position, &qpc_100ns))) break;
        const auto read_ns = SteadyNs();
        if (packets_.size() < 4096) packets_.push_back({qpc_100ns * 100, read_ns, count, flags});
        const auto* samples = reinterpret_cast<const float*>(data);
        for (UINT32 frame = 0; frame < count; ++frame) {
          float peak = 0.0F;
          if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
            for (WORD channel = 0; channel < channels; ++channel)
              peak = std::max(peak, std::abs(samples[frame * channels + channel]));
          }
          const auto offset_ns = static_cast<std::uint64_t>(frame) * 1'000'000'000ULL / rate_;
          const auto remaining_ns = static_cast<std::uint64_t>(count - frame) * 1'000'000'000ULL / rate_;
          frames_.push_back({qpc_100ns * 100 + offset_ns, read_ns - remaining_ns, peak});
        }
        capture_->ReleaseBuffer(count);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CoUninitialize();
  }

  ComPtr<IAudioClient> client_;
  ComPtr<IAudioCaptureClient> capture_;
  WAVEFORMATEX* format_ = nullptr;
  std::uint32_t rate_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::vector<Frame> frames_;
  std::vector<Packet> packets_;
};

struct Tone {
  double phase = 0.0;
};

bool RenderTone(float* output, std::uint32_t frames, std::uint32_t channels, void* context) noexcept {
  auto* tone = static_cast<Tone*>(context);
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const auto value = static_cast<float>(0.1 * std::sin(tone->phase));
    tone->phase += 2.0 * 3.14159265358979323846 * 220.0 / 48000.0;
    if (tone->phase > 6.283185307179586) tone->phase -= 6.283185307179586;
    for (std::uint32_t channel = 0; channel < channels; ++channel) output[frame * channels + channel] = value;
  }
  return true;
}

struct Stats {
  std::size_t count = 0;
  double min = 0, p50 = 0, p95 = 0, p99 = 0, max = 0, mean = 0;
};

Stats Summarize(std::vector<double> values) {
  Stats stats;
  if (values.empty()) return stats;
  std::sort(values.begin(), values.end());
  const auto at = [&](double q) {
    const auto index = static_cast<std::size_t>(std::ceil(q * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
  };
  stats.count = values.size();
  stats.min = values.front();
  stats.max = values.back();
  stats.p50 = at(0.50);
  stats.p95 = at(0.95);
  stats.p99 = at(0.99);
  double sum = 0;
  for (const auto value : values) sum += value;
  stats.mean = sum / values.size();
  return stats;
}

void PrintStats(const char* name, const Stats& stats, bool comma = true) {
  std::printf("  \"%s\": {\"count\": %zu, \"min_ms\": %.3f, \"mean_ms\": %.3f, \"p50_ms\": %.3f, "
              "\"p95_ms\": %.3f, \"p99_ms\": %.3f, \"max_ms\": %.3f}%s\n",
              name, stats.count, stats.min, stats.mean, stats.p50, stats.p95, stats.p99, stats.max,
              comma ? "," : "");
}

void PrintEngineStats(const char* name, const shipglows::audio::SynthLatencyHistogram::Summary& s) {
  std::printf("  \"%s\": {\"count\": %llu, \"min_ms\": %.3f, \"mean_ms\": %.3f, \"p50_bucket_ms\": %.3f, "
              "\"p95_bucket_ms\": %.3f, \"p99_bucket_ms\": %.3f, \"max_ms\": %.3f},\n",
              name, static_cast<unsigned long long>(s.count), s.min_us / 1000.0, s.mean_us / 1000.0,
              s.p50_us / 1000.0, s.p95_us / 1000.0, s.p99_us / 1000.0, s.max_us / 1000.0);
}

}  // namespace

int main(int argc, char** argv) {
  bool low_latency = true;
  bool with_playback = false;
  int clicks = 60;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--default-period") low_latency = false;
    else if (arg == "--with-playback") with_playback = true;
    else if (arg == "--clicks" && i + 1 < argc) clicks = std::max(1, std::atoi(argv[++i]));
    else {
      std::fprintf(stderr, "usage: synth_latency_probe [--default-period] [--with-playback] [--clicks N]\n");
      return 2;
    }
  }
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 3;

  // steady_clock and IAudioClock QPC timestamps must share one time base.
  LARGE_INTEGER counter{}, frequency{};
  QueryPerformanceFrequency(&frequency);
  const auto before = SteadyNs();
  QueryPerformanceCounter(&counter);
  const auto qpc_ns = static_cast<std::uint64_t>(
      static_cast<long double>(counter.QuadPart) * 1e9L / static_cast<long double>(frequency.QuadPart));
  const auto clock_skew_us = (static_cast<double>(qpc_ns) - static_cast<double>(before)) / 1000.0;

  LoopbackCapture capture;
  if (!capture.Start()) {
    std::fprintf(stderr, "loopback capture unavailable\n");
    return 4;
  }
  Tone tone;
  int result = 0;
  {
    WindowsOutputBus bus(low_latency);
    std::uint64_t playback_from = 0, playback_until = 0;
    if (with_playback) {
      if (!bus.AttachPlayback(&RenderTone, &tone)) {
        std::fprintf(stderr, "bus could not start for playback\n");
        return 5;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      playback_from = SteadyNs() + 100'000'000;
    }
    const auto started = bus.StartSynth();
    if (!started) {
      std::fprintf(stderr, "synth output failed: %s hr=0x%08lx\n",
                   shipglows_audio::WasapiPlaybackFailureCode(started.failure), started.hresult);
      return 5;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    bus.ClearSynthLatency();
    std::vector<std::uint64_t> posts;
    posts.reserve(static_cast<std::size_t>(clicks));
    for (int i = 0; i < clicks; ++i) {
      // Irregular spacing decorrelates posts from the device period.
      // Spacing above any plausible output latency keeps onsets unambiguous;
      // the irregular part decorrelates posts from the device period.
      std::this_thread::sleep_for(std::chrono::microseconds(450'000 + (i * 7'919) % 60'000));
      shipglows::audio::SynthEvent click;
      click.type = shipglows::audio::SynthEventType::click;
      click.accent = true;
      const auto posted_at = SteadyNs();
      if (bus.PostSynth(click)) posts.push_back(posted_at);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto metrics = bus.Metrics();
    const auto queued_frames = bus.Timing().queued_frames;
    const auto queue = bus.Synth().queue_latency();
    const auto output = bus.Synth().output_latency();
    const auto counters = bus.Synth().counters();
    bus.StopSynth();
    if (with_playback) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      playback_until = SteadyNs() - 100'000'000;
    }
    const auto starvations = bus.PlaybackStarvations();
    bus.DetachPlayback();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    capture.Stop();

    const auto& frames = capture.frames();
    // Onset threshold: the accented click peaks near 0.7 within 1 ms; the
    // optional tone stays at 0.1.
    const float threshold = with_playback ? 0.3F : 0.05F;
    std::vector<double> mixed_ms, presented_ms;
    std::size_t missed = 0;
    for (std::size_t i = 0; i < posts.size(); ++i) {
      const auto from = posts[i];
      const auto until = i + 1 < posts.size() ? posts[i + 1] : from + 450'000'000;
      const auto onset = [&](auto time_of) -> double {
        for (const auto& frame : frames) {
          const auto at = time_of(frame);
          if (at >= from && at < until && frame.amplitude >= threshold) return (at - from) / 1e6;
        }
        return -1.0;
      };
      const auto mixed = onset([](const Frame& f) { return f.mixed_ns; });
      const auto presented = onset([](const Frame& f) { return f.presented_ns; });
      if (mixed < 0 || presented < 0) {
        ++missed;
        continue;
      }
      mixed_ms.push_back(mixed);
      presented_ms.push_back(presented);
    }
    std::size_t tone_gaps = 0, tone_windows = 0;
    if (with_playback && capture.rate() != 0) {
      // 5 ms windows: the 220 Hz tone peaks at 0.1 in every window.
      const std::size_t window = capture.rate() / 200;
      for (std::size_t first = 0; first + window <= frames.size(); first += window) {
        if (frames[first].mixed_ns < playback_from || frames[first + window - 1].mixed_ns > playback_until)
          continue;
        float peak = 0.0F;
        for (std::size_t i = first; i < first + window; ++i) peak = std::max(peak, frames[i].amplitude);
        ++tone_windows;
        if (peak < 0.05F) ++tone_gaps;
      }
    }

    std::printf("{\n");
    std::printf("  \"mode\": \"%s\",\n", low_latency ? "low_latency_requested" : "default_period");
    char endpoint[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, bus.EndpointName().c_str(), -1, endpoint, sizeof(endpoint) - 1, nullptr, nullptr);
    std::printf("  \"endpoint\": \"%s\",\n", endpoint);
    std::printf("  \"low_latency_granted\": %s,\n", metrics.low_latency ? "true" : "false");
    std::printf("  \"engine_period_frames\": %u,\n", metrics.engine_period_frames);
    std::printf("  \"buffer_frames\": %u,\n", metrics.buffer_frames);
    std::printf("  \"target_fill_frames\": %u,\n", metrics.target_fill_frames);
    std::printf("  \"queued_frames_at_last_clock\": %llu,\n",
                static_cast<unsigned long long>(queued_frames));
    std::printf("  \"stream_latency_ms\": %.3f,\n", metrics.stream_latency_100ns / 10000.0);
    std::printf("  \"loopback_rate\": %u,\n", capture.rate());
    std::printf("  \"steady_vs_qpc_skew_us\": %.1f,\n", clock_skew_us);
    std::printf("  \"clicks_posted\": %zu,\n", posts.size());
    std::printf("  \"clicks_missed_in_loopback\": %zu,\n", missed);
    std::printf("  \"events_rejected\": %llu,\n", static_cast<unsigned long long>(counters.rejected));
    std::printf("  \"callbacks\": %llu,\n", static_cast<unsigned long long>(metrics.callbacks));
    std::printf("  \"device_underruns\": %llu,\n", static_cast<unsigned long long>(metrics.underruns));
    std::printf("  \"p99_callback_us\": %llu,\n", static_cast<unsigned long long>(metrics.p99_callback_microseconds));
    std::printf("  \"max_callback_us\": %llu,\n", static_cast<unsigned long long>(metrics.max_callback_microseconds));
    if (with_playback) {
      std::printf("  \"playback_starvations\": %llu,\n", static_cast<unsigned long long>(starvations));
      std::printf("  \"playback_tone_windows\": %zu,\n", tone_windows);
      std::printf("  \"playback_tone_gaps\": %zu,\n", tone_gaps);
    }
    std::vector<double> read_lag_ms;
    for (const auto& packet : capture.packets())
      read_lag_ms.push_back((static_cast<double>(packet.read_ns) - static_cast<double>(packet.qpc_ns)) / 1e6);
    PrintStats("loopback_read_minus_packet_qpc", Summarize(read_lag_ms));
    PrintEngineStats("queue_wait", queue);
    PrintEngineStats("clock_estimated_output", output);
    PrintStats("loopback_onset_mixed", Summarize(mixed_ms));
    PrintStats("loopback_onset_presented", Summarize(presented_ms), false);
    std::printf("}\n");
    if (missed != 0 || (with_playback && tone_gaps != 0)) result = 1;
  }
  CoUninitialize();
  return result;
}
