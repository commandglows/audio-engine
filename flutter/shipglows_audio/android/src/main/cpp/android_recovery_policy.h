#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace shipglows_audio {

class AndroidRecoveryCoordinator final {
 public:
  using Clock = std::chrono::steady_clock;

  struct Snapshot {
    bool stop_requested;
    bool route_ready;
    std::int32_t device_id;
    std::uint64_t generation;
  };

  enum class Decision { wait, attempt, stop, exhausted };

  static constexpr auto kRecoveryDeadline = std::chrono::seconds(60);
  static constexpr auto kRetryInterval = std::chrono::milliseconds(100);

  void Reset() {
    std::lock_guard lock(mutex_);
    stop_requested_ = false;
    route_ready_ = false;
    device_id_ = -1;
    ++generation_;
    condition_.notify_all();
  }

  void SetRoute(std::int32_t device_id, bool ready) {
    std::lock_guard lock(mutex_);
    const auto resolved_device_id = ready ? device_id : -1;
    if (route_ready_ == ready && device_id_ == resolved_device_id) {
      return;
    }
    route_ready_ = ready;
    device_id_ = resolved_device_id;
    ++generation_;
    condition_.notify_all();
  }

  void RequestStop() {
    std::lock_guard lock(mutex_);
    stop_requested_ = true;
    ++generation_;
    condition_.notify_all();
  }

  [[nodiscard]] Snapshot Current() const {
    std::lock_guard lock(mutex_);
    return Snapshot{stop_requested_, route_ready_, device_id_, generation_};
  }

  [[nodiscard]] Snapshot WaitUntil(Clock::time_point wake_at,
                                   std::uint64_t observed_generation) {
    std::unique_lock lock(mutex_);
    condition_.wait_until(lock, wake_at, [this, observed_generation] {
      return stop_requested_ || generation_ != observed_generation;
    });
    return Snapshot{stop_requested_, route_ready_, device_id_, generation_};
  }

  [[nodiscard]] static bool DeadlineReached(Clock::time_point now,
                                            Clock::time_point deadline) {
    return now >= deadline;
  }

  [[nodiscard]] static Decision Decide(const Snapshot& snapshot,
                                       Clock::time_point now,
                                       Clock::time_point deadline) {
    if (snapshot.stop_requested) {
      return Decision::stop;
    }
    if (DeadlineReached(now, deadline)) {
      return Decision::exhausted;
    }
    if (snapshot.route_ready && snapshot.device_id >= 0) {
      return Decision::attempt;
    }
    return Decision::wait;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool stop_requested_{false};
  bool route_ready_{false};
  std::int32_t device_id_{-1};
  std::uint64_t generation_{0};
};

}  // namespace shipglows_audio
