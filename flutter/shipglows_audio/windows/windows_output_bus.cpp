#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_output_bus.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace shipglows_audio {
namespace {

std::uint64_t SteadyNanoseconds() noexcept {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

void WaitForZero(const std::atomic<std::uint32_t>& counter) noexcept {
  while (counter.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();
}

}  // namespace

WindowsOutputBus::WindowsOutputBus(bool low_latency)
    : synth_(std::make_unique<shipglows::audio::SynthEngine>()) {
  WasapiA0Observer observer;
  observer.submitted = &WindowsOutputBus::Submitted;
  observer.clock = &WindowsOutputBus::Clock;
  observer.context = this;
  observer.analysis_submitted = &WindowsOutputBus::AnalysisSubmitted;
  observer.analysis_context = this;
  device_ = std::make_unique<WindowsWasapiPlayback>(&WindowsOutputBus::Render, this,
                                                    observer, low_latency);
}

WindowsOutputBus::~WindowsOutputBus() {
  device_.reset();
}

WasapiPlaybackResult WindowsOutputBus::AttachPlayback(WasapiRenderFunction render,
                                                      void* context) {
  if (!render || !context) return {WasapiPlaybackFailure::initialization_failed, 0};
  DetachPlayback();
  playback_render_ = render;
  playback_context_.store(context, std::memory_order_seq_cst);
  const auto result = EnsureRunning();
  if (!result) DetachPlayback();
  return result;
}

void WindowsOutputBus::DetachPlayback() noexcept {
  playback_context_.exchange(nullptr, std::memory_order_seq_cst);
  WaitForZero(render_active_);
  StopIfIdle();
}

bool WindowsOutputBus::PlaybackAttached() const noexcept {
  return playback_context_.load(std::memory_order_seq_cst) != nullptr;
}

std::uint64_t WindowsOutputBus::PlaybackStarvations() const noexcept {
  return playback_starvations_.load(std::memory_order_relaxed);
}

WasapiPlaybackResult WindowsOutputBus::StartSynth() {
  synth_enabled_ = true;
  const auto result = EnsureRunning();
  if (!result) synth_enabled_ = false;
  return result;
}

void WindowsOutputBus::StopSynth() {
  if (!synth_enabled_) return;
  shipglows::audio::SynthEvent off;
  off.type = shipglows::audio::SynthEventType::all_notes_off;
  (void)PostSynth(off);
  synth_enabled_ = false;
  // Let the 5 ms all-notes-off fade reach the device before releasing it.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
  while (Running() && synth_->counters().active_voices != 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  StopIfIdle();
}

bool WindowsOutputBus::PostSynth(shipglows::audio::SynthEvent event) noexcept {
  if (!synth_enabled_ && event.type != shipglows::audio::SynthEventType::all_notes_off)
    return false;
  event.post_ns = SteadyNanoseconds();
  return synth_->post(event);
}

void WindowsOutputBus::SetAnalysisStream(shipglows::audio::AnalysisStream* stream) noexcept {
  ClearAnalysisStream();
  analysis_stream_.store(stream, std::memory_order_seq_cst);
}

void WindowsOutputBus::ClearAnalysisStream() noexcept {
  analysis_stream_.exchange(nullptr, std::memory_order_seq_cst);
  WaitForZero(analysis_active_);
}

bool WindowsOutputBus::Running() const noexcept {
  return device_->Metrics().sample_rate != 0;
}

WasapiPlaybackMetrics WindowsOutputBus::Metrics() const noexcept { return device_->Metrics(); }

WasapiPlaybackResult WindowsOutputBus::Status() const noexcept { return device_->Status(); }

std::wstring WindowsOutputBus::EndpointName() const { return device_->EndpointName(); }

OutputBusTiming WindowsOutputBus::Timing() const noexcept {
  OutputBusTiming timing;
  timing.valid = timing_valid_.load(std::memory_order_acquire);
  timing.queued_frames = queued_frames_.load(std::memory_order_relaxed);
  return timing;
}

WasapiPlaybackResult WindowsOutputBus::EnsureRunning() {
  if (Running()) return {};
  // The callback is stopped: the worker's command handshake publishes these
  // writes before the new stream renders its first block.
  master_.reset();
  // Events queued while the device was down would otherwise play as a burst.
  synth_->reset();
  reset_written_ = true;
  clock_valid_ = false;
  timing_valid_.store(false, std::memory_order_release);
  return device_->Start();
}

void WindowsOutputBus::StopIfIdle() {
  if (synth_enabled_ || PlaybackAttached() || !Running()) return;
  (void)device_->Stop();
  timing_valid_.store(false, std::memory_order_release);
}

bool WindowsOutputBus::Render(float* output, std::uint32_t frames, std::uint32_t channels,
                              void* context) noexcept {
  auto* self = static_cast<WindowsOutputBus*>(context);
  if (!self || !output || channels != 2) return false;
  const auto now_ns = SteadyNanoseconds();
  self->render_active_.fetch_add(1, std::memory_order_seq_cst);
  void* source = self->playback_context_.load(std::memory_order_seq_cst);
  bool rendered = false;
  if (source) {
    rendered = self->playback_render_(output, frames, channels, source);
    if (!rendered) self->playback_starvations_.fetch_add(1, std::memory_order_relaxed);
  }
  self->render_active_.fetch_sub(1, std::memory_order_seq_cst);
  if (!rendered) std::fill(output, output + static_cast<std::size_t>(frames) * 2, 0.0F);

  std::uint64_t presentation_ns = 0;
  if (self->clock_valid_ && self->written_frames_ >= self->clock_position_frames_) {
    const auto ahead = self->written_frames_ - self->clock_position_frames_;
    presentation_ns = self->clock_qpc_ns_ + ahead * 1'000'000'000ULL / 48'000ULL;
  }
  self->synth_->render_add(output, frames, now_ns, presentation_ns);
  self->master_.process(output, frames);
  return true;
}

void WindowsOutputBus::Submitted(const float*, std::uint32_t frames, bool, bool,
                                 void* context) noexcept {
  auto* self = static_cast<WindowsOutputBus*>(context);
  if (!self) return;
  // The first submission after Start is the silent prefill of a new client,
  // whose device position restarts from zero.
  if (self->reset_written_) {
    self->reset_written_ = false;
    self->written_frames_ = 0;
  }
  self->written_frames_ += frames;
}

void WindowsOutputBus::AnalysisSubmitted(const float* pcm, std::uint32_t frames,
                                         std::uint64_t position_frames, bool render_failed,
                                         bool submission_succeeded, void* context) noexcept {
  auto* self = static_cast<WindowsOutputBus*>(context);
  if (!self) return;
  self->analysis_active_.fetch_add(1, std::memory_order_seq_cst);
  if (auto* stream = self->analysis_stream_.load(std::memory_order_seq_cst)) {
    (void)stream->submit(pcm, frames, position_frames, render_failed, submission_succeeded);
  }
  self->analysis_active_.fetch_sub(1, std::memory_order_seq_cst);
}

void WindowsOutputBus::Clock(std::uint64_t ticks, std::uint64_t frequency,
                             std::uint64_t qpc_100ns, std::uint64_t, std::uint64_t,
                             long status, void* context) noexcept {
  auto* self = static_cast<WindowsOutputBus*>(context);
  if (!self || status < 0 || frequency == 0) return;
  const auto position = static_cast<std::uint64_t>(
      static_cast<long double>(ticks) * 48'000.0L / static_cast<long double>(frequency));
  self->clock_position_frames_ = position;
  // IAudioClock reports QPC time in 100 ns units; steady_clock is QPC-based.
  self->clock_qpc_ns_ = qpc_100ns * 100;
  self->clock_valid_ = true;
  self->queued_frames_.store(self->written_frames_ > position ? self->written_frames_ - position : 0,
                             std::memory_order_relaxed);
  self->timing_valid_.store(true, std::memory_order_release);
}

BusPlaybackOutput::BusPlaybackOutput(WindowsOutputBus* bus, WasapiRenderFunction render,
                                     void* context)
    : bus_(bus), render_(render), context_(context),
      starvations_at_creation_(bus ? bus->PlaybackStarvations() : 0) {}

BusPlaybackOutput::~BusPlaybackOutput() {
  if (bus_) bus_->DetachPlayback();
}

WasapiPlaybackResult BusPlaybackOutput::Start() {
  if (!bus_) return {WasapiPlaybackFailure::initialization_failed, 0};
  return bus_->AttachPlayback(render_, context_);
}

WasapiPlaybackResult BusPlaybackOutput::Pause() {
  if (!bus_ || !bus_->PlaybackAttached()) return {WasapiPlaybackFailure::control_failed, 0};
  bus_->DetachPlayback();
  return {};
}

WasapiPlaybackResult BusPlaybackOutput::Resume() { return Start(); }

WasapiPlaybackResult BusPlaybackOutput::Stop() {
  if (bus_) bus_->DetachPlayback();
  return {};
}

WasapiPlaybackMetrics BusPlaybackOutput::Metrics() const noexcept {
  if (!bus_) return {};
  auto metrics = bus_->Metrics();
  metrics.underruns += bus_->PlaybackStarvations() - starvations_at_creation_;
  return metrics;
}

WasapiPlaybackResult BusPlaybackOutput::Status() const noexcept {
  return bus_ ? bus_->Status() : WasapiPlaybackResult{};
}

std::wstring BusPlaybackOutput::EndpointName() const {
  return bus_ ? bus_->EndpointName() : std::wstring{};
}

}  // namespace shipglows_audio
