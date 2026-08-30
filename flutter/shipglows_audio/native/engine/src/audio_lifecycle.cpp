#include "shipglows/audio/audio_lifecycle.hpp"

#include <algorithm>
#include <limits>

namespace shipglows::audio {

AudioLifecycle::AudioLifecycle(ReconnectPolicy policy) noexcept
    : policy_(policy) {
  if (policy_.initial_delay.count() < 0) {
    policy_.initial_delay = std::chrono::milliseconds::zero();
  }
  if (policy_.maximum_delay < policy_.initial_delay) {
    policy_.maximum_delay = policy_.initial_delay;
  }
}

bool AudioLifecycle::start() noexcept {
  if (snapshot_.state != LifecycleState::stopped) {
    return false;
  }
  snapshot_ = {.state = LifecycleState::running,
               .reason = InterruptionReason::none,
               .generation = snapshot_.generation + 1,
               .reconnect_attempt = 0};
  return true;
}

bool AudioLifecycle::pause() noexcept {
  if (snapshot_.state != LifecycleState::running) {
    return false;
  }
  snapshot_.state = LifecycleState::user_paused;
  snapshot_.reason = InterruptionReason::user_pause;
  return true;
}

bool AudioLifecycle::resume() noexcept {
  if (snapshot_.state != LifecycleState::user_paused) {
    return false;
  }
  snapshot_.state = LifecycleState::running;
  snapshot_.reason = InterruptionReason::none;
  ++snapshot_.generation;
  return true;
}

bool AudioLifecycle::interrupt(InterruptionReason reason) noexcept {
  if (snapshot_.state != LifecycleState::running ||
      reason == InterruptionReason::none ||
      reason == InterruptionReason::user_pause) {
    return false;
  }
  snapshot_.state = LifecycleState::interrupted;
  snapshot_.reason = reason;
  snapshot_.reconnect_attempt = 0;
  return true;
}

bool AudioLifecycle::begin_reconnect() noexcept {
  if ((snapshot_.state != LifecycleState::interrupted &&
       snapshot_.state != LifecycleState::reconnecting) ||
      !should_reconnect()) {
    return false;
  }
  snapshot_.state = LifecycleState::reconnecting;
  ++snapshot_.reconnect_attempt;
  return true;
}

bool AudioLifecycle::reconnect_succeeded() noexcept {
  if (snapshot_.state != LifecycleState::reconnecting) {
    return false;
  }
  snapshot_.state = LifecycleState::running;
  snapshot_.reason = InterruptionReason::none;
  snapshot_.reconnect_attempt = 0;
  ++snapshot_.generation;
  return true;
}

bool AudioLifecycle::reconnect_failed() noexcept {
  if (snapshot_.state != LifecycleState::reconnecting) {
    return false;
  }
  if (snapshot_.reconnect_attempt >= policy_.max_attempts) {
    snapshot_.state = LifecycleState::failed;
    return false;
  }
  return true;
}

void AudioLifecycle::stop() noexcept {
  snapshot_.state = LifecycleState::stopped;
  snapshot_.reason = InterruptionReason::none;
  snapshot_.reconnect_attempt = 0;
}

void AudioLifecycle::fail() noexcept { snapshot_.state = LifecycleState::failed; }

bool AudioLifecycle::should_reconnect() const noexcept {
  return policy_.max_attempts > 0 &&
         snapshot_.reconnect_attempt < policy_.max_attempts &&
         (snapshot_.state == LifecycleState::interrupted ||
          snapshot_.state == LifecycleState::reconnecting);
}

std::chrono::milliseconds AudioLifecycle::reconnect_delay() const noexcept {
  if (snapshot_.reconnect_attempt == 0) {
    return policy_.initial_delay;
  }
  auto delay = policy_.initial_delay;
  for (std::uint32_t index = 1; index < snapshot_.reconnect_attempt; ++index) {
    if (delay >= policy_.maximum_delay / 2) {
      return policy_.maximum_delay;
    }
    delay *= 2;
  }
  return std::min(delay, policy_.maximum_delay);
}

LifecycleSnapshot AudioLifecycle::snapshot() const noexcept { return snapshot_; }

TimestampTracker::TimestampTracker(std::uint32_t sample_rate) noexcept
    : sample_rate_(sample_rate) {}

void TimestampTracker::reset(std::uint32_t sample_rate,
                             std::uint64_t generation) noexcept {
  sample_rate_ = sample_rate;
  generation_ = generation;
  expected_next_time_ns_ = 0;
  last_position_ = 0;
  last_position_time_ns_ = 0;
  initialized_ = false;
  position_initialized_ = false;
}

std::uint64_t TimestampTracker::observe_position(
    std::uint64_t host_time_ns, std::uint64_t frame_position,
    std::uint64_t generation) noexcept {
  if (sample_rate_ == 0 || host_time_ns == 0) return 0;
  if (!position_initialized_ || generation != generation_) {
    generation_ = generation;
    last_position_ = frame_position;
    last_position_time_ns_ = host_time_ns;
    position_initialized_ = true;
    initialized_ = false;
    return 0;
  }
  if (frame_position < last_position_ || host_time_ns <= last_position_time_ns_) {
    last_position_ = frame_position;
    last_position_time_ns_ = host_time_ns;
    return 0;
  }
  const auto elapsed_ns = host_time_ns - last_position_time_ns_;
  const auto expected_frames =
      (elapsed_ns * sample_rate_ + 500'000'000ULL) / 1'000'000'000ULL;
  const auto actual_frames = frame_position - last_position_;
  const auto tolerance = std::max<std::uint64_t>(1, sample_rate_ / 1000);
  const auto gap = actual_frames > expected_frames + tolerance
                       ? actual_frames - expected_frames
                       : 0;
  last_position_ = frame_position;
  last_position_time_ns_ = host_time_ns;
  return gap;
}

std::uint64_t TimestampTracker::observe(std::uint64_t host_time_ns,
                                        std::uint32_t frame_count,
                                        std::uint64_t generation) noexcept {
  if (sample_rate_ == 0 || host_time_ns == 0 || frame_count == 0) {
    return 0;
  }
  if (!initialized_ || generation != generation_) {
    generation_ = generation;
    initialized_ = true;
    expected_next_time_ns_ = host_time_ns +
        (static_cast<std::uint64_t>(frame_count) * 1'000'000'000ULL) /
            sample_rate_;
    return 0;
  }

  const auto tolerance_ns = 1'000'000'000ULL / sample_rate_;
  std::uint64_t gap_frames = 0;
  if (host_time_ns > expected_next_time_ns_ + tolerance_ns) {
    const auto gap_ns = host_time_ns - expected_next_time_ns_;
    gap_frames = (gap_ns * sample_rate_ + 500'000'000ULL) / 1'000'000'000ULL;
  }
  expected_next_time_ns_ = host_time_ns +
      (static_cast<std::uint64_t>(frame_count) * 1'000'000'000ULL) /
          sample_rate_;
  return gap_frames;
}

}  // namespace shipglows::audio
