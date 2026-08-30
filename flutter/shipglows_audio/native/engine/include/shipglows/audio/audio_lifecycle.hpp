#pragma once

#include <chrono>
#include <cstdint>

namespace shipglows::audio {

enum class LifecycleState : std::uint8_t {
  stopped,
  running,
  user_paused,
  interrupted,
  reconnecting,
  failed,
};

enum class InterruptionReason : std::uint8_t {
  none,
  user_pause,
  platform_interruption,
  device_disconnected,
  route_changed,
  system_suspended,
};

struct ReconnectPolicy final {
  std::uint32_t max_attempts = 5;
  std::chrono::milliseconds initial_delay{100};
  std::chrono::milliseconds maximum_delay{2'000};
};

struct LifecycleSnapshot final {
  LifecycleState state = LifecycleState::stopped;
  InterruptionReason reason = InterruptionReason::none;
  std::uint64_t generation = 0;
  std::uint32_t reconnect_attempt = 0;
};

class AudioLifecycle final {
 public:
  explicit AudioLifecycle(ReconnectPolicy policy = {}) noexcept;

  [[nodiscard]] bool start() noexcept;
  [[nodiscard]] bool pause() noexcept;
  [[nodiscard]] bool resume() noexcept;
  [[nodiscard]] bool interrupt(InterruptionReason reason) noexcept;
  [[nodiscard]] bool begin_reconnect() noexcept;
  [[nodiscard]] bool reconnect_succeeded() noexcept;
  [[nodiscard]] bool reconnect_failed() noexcept;
  void stop() noexcept;
  void fail() noexcept;

  [[nodiscard]] bool should_reconnect() const noexcept;
  [[nodiscard]] std::chrono::milliseconds reconnect_delay() const noexcept;
  [[nodiscard]] LifecycleSnapshot snapshot() const noexcept;

 private:
  ReconnectPolicy policy_;
  LifecycleSnapshot snapshot_;
};

class TimestampTracker final {
 public:
  explicit TimestampTracker(std::uint32_t sample_rate = 0) noexcept;

  void reset(std::uint32_t sample_rate, std::uint64_t generation) noexcept;
  [[nodiscard]] std::uint64_t observe(std::uint64_t host_time_ns,
                                      std::uint32_t frame_count,
                                      std::uint64_t generation) noexcept;
  [[nodiscard]] std::uint64_t observe_position(
      std::uint64_t host_time_ns, std::uint64_t frame_position,
      std::uint64_t generation) noexcept;

 private:
  std::uint32_t sample_rate_ = 0;
  std::uint64_t generation_ = 0;
  std::uint64_t expected_next_time_ns_ = 0;
  std::uint64_t last_position_ = 0;
  std::uint64_t last_position_time_ns_ = 0;
  bool initialized_ = false;
  bool position_initialized_ = false;
};

}  // namespace shipglows::audio
