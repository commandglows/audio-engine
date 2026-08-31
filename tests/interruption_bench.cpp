#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "shipglows/audio/audio_lifecycle.hpp"
#include "shipglows/audio/recording_preflight.hpp"
#include "shipglows/audio/segmented_wav_store.hpp"
#include "android_recovery_policy.h"

namespace {

using shipglows::audio::AudioFormat;
using shipglows::audio::AudioLifecycle;
using shipglows::audio::InterruptionReason;
using shipglows::audio::SampleFormat;
using shipglows::audio::SegmentedWavStore;
using shipglows::audio::SessionEvent;
using shipglows::audio::TimestampTracker;
using shipglows::audio::WavStoreFaultPolicy;
using shipglows_audio::AndroidRecoveryCoordinator;

constexpr AudioFormat kFormat{48'000, 1, SampleFormat::int16};

std::string read_journal(const std::filesystem::path& root) {
  std::ifstream input(root / "journal.sga");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void append_frames(SegmentedWavStore& store, std::size_t frames) {
  std::vector<std::int16_t> samples(frames, 42);
  store.append(std::as_bytes(std::span(samples)));
}

void exercise_incident(const std::filesystem::path& root, SessionEvent event,
                       const char* reason) {
  std::filesystem::remove_all(root);
  SegmentedWavStore store(root, kFormat, 32);
  append_frames(store, 40);
  store.checkpoint(event, reason);
  append_frames(store, 24);
  store.checkpoint(SessionEvent::device_restart, "recovered");
  append_frames(store, 8);
  store.finalize();
  assert(store.total_frames() == 72);
  assert(store.segments().size() >= 3);
  const auto journal = read_journal(root);
  assert(journal.find(std::string("reason=") + reason) != std::string::npos);
  assert(journal.find("event=session_complete,total_frames=72") !=
         std::string::npos);
}

void test_interruption_matrix(const std::filesystem::path& root) {
  exercise_incident(root / "disconnect", SessionEvent::interruption,
                    "device_disconnected");
  exercise_incident(root / "route", SessionEvent::route_change,
                    "default_device_changed");
  exercise_incident(root / "audio_loss", SessionEvent::interruption,
                    "platform_interruption");
  exercise_incident(root / "suspend", SessionEvent::interruption,
                    "system_suspended");

  const auto pause_root = root / "pause_resume";
  std::filesystem::remove_all(pause_root);
  SegmentedWavStore store(pause_root, kFormat, 64);
  for (int index = 0; index < 20; ++index) {
    append_frames(store, 4);
    store.checkpoint(SessionEvent::pause, "user_pause");
    store.checkpoint(SessionEvent::resume, "user_resume");
  }
  store.finalize();
  assert(store.total_frames() == 80);
  assert(read_journal(pause_root).find("event=resume") != std::string::npos);
}

void test_bounded_reconnect_and_interrupted_reconnect() {
  AudioLifecycle lifecycle;
  assert(lifecycle.start());
  assert(lifecycle.interrupt(InterruptionReason::device_disconnected));
  std::array<long long, 5> observed{};
  for (auto& delay : observed) {
    assert(lifecycle.begin_reconnect());
    delay = lifecycle.reconnect_delay().count();
    static_cast<void>(lifecycle.reconnect_failed());
  }
  assert((observed == std::array<long long, 5>{100, 200, 400, 800, 1600}));
  assert(!lifecycle.should_reconnect());

  AudioLifecycle interrupted;
  assert(interrupted.start());
  assert(interrupted.interrupt(InterruptionReason::route_changed));
  assert(interrupted.begin_reconnect());
  interrupted.stop();
  assert(!interrupted.should_reconnect());
}

void test_android_recovery_deadline_and_device_handoff() {
  using namespace std::chrono_literals;
  using Clock = AndroidRecoveryCoordinator::Clock;

  const auto start = Clock::time_point{};
  static_assert(AndroidRecoveryCoordinator::kRecoveryDeadline == 60s);
  const auto deadline = start + AndroidRecoveryCoordinator::kRecoveryDeadline;
  const AndroidRecoveryCoordinator::Snapshot unavailable_after_3100_ms{
      false, false, -1, 1};
  assert(AndroidRecoveryCoordinator::Decide(unavailable_after_3100_ms,
                                             start + 3100ms, deadline) ==
         AndroidRecoveryCoordinator::Decision::wait);
  const AndroidRecoveryCoordinator::Snapshot returned_after_3200_ms{
      false, true, 42, 2};
  assert(AndroidRecoveryCoordinator::Decide(returned_after_3200_ms,
                                             start + 3200ms, deadline) ==
         AndroidRecoveryCoordinator::Decision::attempt);
  assert(!AndroidRecoveryCoordinator::DeadlineReached(start + 3100ms,
                                                       deadline));
  assert(!AndroidRecoveryCoordinator::DeadlineReached(deadline - 1ms,
                                                       deadline));
  assert(AndroidRecoveryCoordinator::DeadlineReached(deadline, deadline));
  assert(AndroidRecoveryCoordinator::Decide(returned_after_3200_ms, deadline,
                                             deadline) ==
         AndroidRecoveryCoordinator::Decision::exhausted);
  assert(AndroidRecoveryCoordinator::DeadlineReached(deadline + 1ms,
                                                      deadline));
  const AndroidRecoveryCoordinator::Snapshot stopped_at_deadline{
      true, true, 42, 3};
  assert(AndroidRecoveryCoordinator::Decide(stopped_at_deadline, deadline,
                                             deadline) ==
         AndroidRecoveryCoordinator::Decision::stop);

  AndroidRecoveryCoordinator coordinator;
  coordinator.Reset();
  const auto unavailable = coordinator.Current();
  assert(!unavailable.route_ready && unavailable.device_id == -1);

  coordinator.SetRoute(17, true);
  const auto first = coordinator.Current();
  assert(first.route_ready && first.device_id == 17);
  coordinator.SetRoute(17, true);
  assert(coordinator.Current().generation == first.generation);

  coordinator.SetRoute(-1, false);
  coordinator.SetRoute(42, true);
  const auto reacquired = coordinator.Current();
  assert(reacquired.route_ready && reacquired.device_id == 42);
  assert(reacquired.generation == first.generation + 2);
}

void test_android_recovery_wait_is_cancellable() {
  using namespace std::chrono_literals;
  AndroidRecoveryCoordinator coordinator;
  coordinator.Reset();

  const auto wait_for_stop = [&coordinator] {
    const auto initial = coordinator.Current();
    return coordinator.WaitUntil(AndroidRecoveryCoordinator::Clock::now() + 5s,
                                 initial.generation);
  };

  coordinator.RequestStop();
  auto immediate = std::async(std::launch::async, wait_for_stop);
  assert(immediate.wait_for(100ms) == std::future_status::ready);
  assert(immediate.get().stop_requested);

  coordinator.Reset();
  auto during_wait = std::async(std::launch::async, wait_for_stop);
  std::this_thread::sleep_for(10ms);
  coordinator.RequestStop();
  assert(during_wait.wait_for(100ms) == std::future_status::ready);
  assert(during_wait.get().stop_requested);

  coordinator.Reset();
  const auto before_route = coordinator.Current();
  auto route_wait = std::async(std::launch::async, [&coordinator, before_route] {
    return coordinator.WaitUntil(AndroidRecoveryCoordinator::Clock::now() + 5s,
                                 before_route.generation);
  });
  coordinator.SetRoute(99, true);
  assert(route_wait.wait_for(100ms) == std::future_status::ready);
  const auto routed = route_wait.get();
  assert(!routed.stop_requested && routed.route_ready && routed.device_id == 99);
}

void test_storage_failures_preserve_committed_segments(
    const std::filesystem::path& root) {
  const auto write_root = root / "write_failure";
  std::filesystem::remove_all(write_root);
  bool failed = false;
  try {
    SegmentedWavStore store(write_root, kFormat, 16,
                            WavStoreFaultPolicy{.fail_after_frames = 24});
    append_frames(store, 48);
  } catch (const std::runtime_error&) {
    failed = true;
  }
  assert(failed);
  assert(std::filesystem::exists(write_root / "segment-000000.wav"));
  const auto journal = read_journal(write_root);
  assert(journal.find("reason=simulated_write_failure") != std::string::npos);
  assert(journal.find("segment=0,0,16") != std::string::npos);

  const auto no_space = shipglows::audio::recording_preflight(
      root / "no_space",
      {.minimum_free_bytes = std::numeric_limits<std::uint64_t>::max()});
  assert(!no_space.ready && no_space.error_code == "insufficient_storage");
}

void test_hardware_timestamp_continuity() {
  TimestampTracker tracker(48'000);
  assert(tracker.observe_position(1'000'000'000ULL, 0, 1) == 0);
  assert(tracker.observe_position(1'100'000'000ULL, 4'800, 1) == 0);
  // Segment rotation does not change the generation and remains continuous.
  assert(tracker.observe_position(1'200'000'000ULL, 9'600, 1) == 0);
  assert(tracker.observe_position(1'300'000'000ULL, 15'000, 1) == 600);
  // A recovered hardware stream starts a new clock generation.
  assert(tracker.observe_position(2'000'000'000ULL, 0, 2) == 0);
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    "shipglows-audio-interruption-bench";
  std::filesystem::remove_all(root);
  test_interruption_matrix(root);
  test_bounded_reconnect_and_interrupted_reconnect();
  test_android_recovery_deadline_and_device_handoff();
  test_android_recovery_wait_is_cancellable();
  test_storage_failures_preserve_committed_segments(root);
  test_hardware_timestamp_continuity();
  std::filesystem::remove_all(root);
  std::cout << "shipglows_audio_interruption_bench: passed\n";
}
