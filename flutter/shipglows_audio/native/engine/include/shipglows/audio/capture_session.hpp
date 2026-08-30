#pragma once

#include <atomic>
#include <cstdint>

#include "shipglows/audio/audio_types.hpp"

namespace shipglows::audio {

struct CaptureMetrics final {
  std::uint64_t frames_captured = 0;
  std::uint64_t frames_dropped = 0;
  std::uint64_t discontinuities = 0;
  std::uint64_t clipped_samples = 0;
  std::uint64_t device_restarts = 0;
};

class CaptureSession final {
 public:
  [[nodiscard]] bool prepare(AudioFormat format) noexcept;
  [[nodiscard]] bool start() noexcept;
  [[nodiscard]] bool request_stop() noexcept;
  [[nodiscard]] bool finish() noexcept;
  void fail() noexcept;

  void count_captured_frames(std::uint64_t frames) noexcept;
  void count_dropped_frames(std::uint64_t frames) noexcept;
  void count_discontinuity() noexcept;
  void count_clipped_samples(std::uint64_t samples) noexcept;
  void count_device_restart() noexcept;

  [[nodiscard]] SessionState state() const noexcept;
  [[nodiscard]] AudioFormat format() const noexcept;
  [[nodiscard]] CaptureMetrics metrics() const noexcept;

 private:
  std::atomic<SessionState> state_{SessionState::idle};
  AudioFormat format_{};
  std::atomic<std::uint64_t> frames_captured_{0};
  std::atomic<std::uint64_t> frames_dropped_{0};
  std::atomic<std::uint64_t> discontinuities_{0};
  std::atomic<std::uint64_t> clipped_samples_{0};
  std::atomic<std::uint64_t> device_restarts_{0};
};

}  // namespace shipglows::audio
