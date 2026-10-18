#ifndef SHIPGLOWS_AUDIO_WINDOWS_OUTPUT_BUS_H_
#define SHIPGLOWS_AUDIO_WINDOWS_OUTPUT_BUS_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "shipglows/audio/analysis_stream.hpp"
#include "shipglows/audio/dsp_chain.hpp"
#include "shipglows/audio/synth_engine.hpp"
#include "windows_wasapi_playback.h"

namespace shipglows_audio {

// Presentation timing derived from IAudioClock after each submission.
struct OutputBusTiming final {
  bool valid = false;
  // Frames submitted but not yet presented at the last clock sample.
  std::uint64_t queued_frames = 0;
};

// One shared WASAPI render stream for the process. The callback mixes the
// attached file-playback source and the synthesizer, applies one master
// true-peak limiter, and feeds the analysis tap with the exact submitted PCM.
//
// Every public method belongs to the plugin's platform thread. Inputs are
// published through atomics and detached with a callback fence, so the render
// path never locks, allocates or waits.
class WindowsOutputBus final {
 public:
  explicit WindowsOutputBus(bool low_latency = true);
  ~WindowsOutputBus();
  WindowsOutputBus(const WindowsOutputBus&) = delete;
  WindowsOutputBus& operator=(const WindowsOutputBus&) = delete;

  // Attaches a playback source and starts the device when needed. After
  // DetachPlayback returns, the callback no longer touches the source.
  WasapiPlaybackResult AttachPlayback(WasapiRenderFunction render, void* context);
  void DetachPlayback() noexcept;
  [[nodiscard]] bool PlaybackAttached() const noexcept;
  [[nodiscard]] std::uint64_t PlaybackStarvations() const noexcept;

  WasapiPlaybackResult StartSynth();
  // Fades every voice out, then releases the device when nothing else plays.
  void StopSynth();
  [[nodiscard]] bool SynthEnabled() const noexcept { return synth_enabled_; }
  // Stamps the steady-clock post time; false when disabled or queue full.
  bool PostSynth(shipglows::audio::SynthEvent event) noexcept;
  [[nodiscard]] const shipglows::audio::SynthEngine& Synth() const noexcept { return *synth_; }
  void ClearSynthLatency() noexcept { synth_->clear_latency(); }

  // The analysis tap observes the mixed, limited output. Clear fences the
  // callback before returning so the caller may free the stream.
  void SetAnalysisStream(shipglows::audio::AnalysisStream* stream) noexcept;
  void ClearAnalysisStream() noexcept;

  [[nodiscard]] bool Running() const noexcept;
  [[nodiscard]] WasapiPlaybackMetrics Metrics() const noexcept;
  [[nodiscard]] WasapiPlaybackResult Status() const noexcept;
  [[nodiscard]] std::wstring EndpointName() const;
  [[nodiscard]] OutputBusTiming Timing() const noexcept;

 private:
  WasapiPlaybackResult EnsureRunning();
  void StopIfIdle();
  static bool Render(float* output, std::uint32_t frames, std::uint32_t channels,
                     void* context) noexcept;
  static void Submitted(const float* pcm, std::uint32_t frames, bool render_failed,
                        bool submission_succeeded, void* context) noexcept;
  static void AnalysisSubmitted(const float* pcm, std::uint32_t frames,
                                std::uint64_t position_frames, bool render_failed,
                                bool submission_succeeded, void* context) noexcept;
  static void Clock(std::uint64_t ticks, std::uint64_t frequency, std::uint64_t qpc_100ns,
                    std::uint64_t begin_ns, std::uint64_t end_ns, long status,
                    void* context) noexcept;

  std::unique_ptr<shipglows::audio::SynthEngine> synth_;
  bool synth_enabled_ = false;  // platform thread
  // Playback input: render_ is written before context_ is published.
  WasapiRenderFunction playback_render_ = nullptr;
  std::atomic<void*> playback_context_{nullptr};
  std::atomic<std::uint32_t> render_active_{0};
  std::atomic<std::uint64_t> playback_starvations_{0};
  std::atomic<shipglows::audio::AnalysisStream*> analysis_stream_{nullptr};
  std::atomic<std::uint32_t> analysis_active_{0};

  // Callback-thread state.
  shipglows::audio::DspChain master_{48'000};
  bool reset_written_ = true;  // set by the platform thread while stopped
  std::uint64_t written_frames_ = 0;
  bool clock_valid_ = false;
  std::uint64_t clock_position_frames_ = 0;
  std::uint64_t clock_qpc_ns_ = 0;
  std::atomic<std::uint64_t> queued_frames_{0};
  std::atomic<bool> timing_valid_{false};

  std::unique_ptr<WindowsWasapiPlayback> device_;
};

// Playback-session view of the bus with the former per-session output API.
// Start/Resume attach the source; Pause/Stop detach it and quiesce its
// callback. The device itself keeps running while the synthesizer plays.
class BusPlaybackOutput final {
 public:
  BusPlaybackOutput(WindowsOutputBus* bus, WasapiRenderFunction render, void* context);
  ~BusPlaybackOutput();
  BusPlaybackOutput(const BusPlaybackOutput&) = delete;
  BusPlaybackOutput& operator=(const BusPlaybackOutput&) = delete;

  WasapiPlaybackResult Start();
  WasapiPlaybackResult Pause();
  WasapiPlaybackResult Resume();
  WasapiPlaybackResult Stop();
  // Device metrics; underruns add this session's source starvations.
  [[nodiscard]] WasapiPlaybackMetrics Metrics() const noexcept;
  [[nodiscard]] WasapiPlaybackResult Status() const noexcept;
  [[nodiscard]] std::wstring EndpointName() const;

 private:
  WindowsOutputBus* bus_;
  WasapiRenderFunction render_;
  void* context_;
  std::uint64_t starvations_at_creation_;
};

}  // namespace shipglows_audio

#endif  // SHIPGLOWS_AUDIO_WINDOWS_OUTPUT_BUS_H_
