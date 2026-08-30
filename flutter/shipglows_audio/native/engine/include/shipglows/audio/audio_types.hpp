#pragma once

#include <cstdint>
#include <string_view>

namespace shipglows::audio {

enum class SampleFormat : std::uint8_t {
  int16,
  int24,
  int32,
  float32,
};

struct AudioFormat final {
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
  SampleFormat sample_format = SampleFormat::int16;

  [[nodiscard]] constexpr std::uint16_t bytes_per_sample() const noexcept {
    switch (sample_format) {
      case SampleFormat::int16:
        return 2;
      case SampleFormat::int24:
        return 3;
      case SampleFormat::int32:
      case SampleFormat::float32:
        return 4;
    }
    return 0;
  }

  [[nodiscard]] constexpr std::uint32_t bytes_per_frame() const noexcept {
    return static_cast<std::uint32_t>(channel_count) * bytes_per_sample();
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return sample_rate >= 8'000 && sample_rate <= 384'000 &&
           channel_count >= 1 && channel_count <= 32 && bytes_per_frame() > 0;
  }
};

enum class SessionState : std::uint8_t {
  idle,
  prepared,
  recording,
  paused,
  stopping,
  stopped,
  failed,
};

enum class DiagnosticCode : std::uint8_t {
  dropout,
  clipping,
  device_invalidated,
  storage_stalled,
  segment_recovered,
};

[[nodiscard]] constexpr std::string_view to_string(SessionState state) noexcept {
  switch (state) {
    case SessionState::idle:
      return "idle";
    case SessionState::prepared:
      return "prepared";
    case SessionState::recording:
      return "recording";
    case SessionState::paused:
      return "paused";
    case SessionState::stopping:
      return "stopping";
    case SessionState::stopped:
      return "stopped";
    case SessionState::failed:
      return "failed";
  }
  return "unknown";
}

}  // namespace shipglows::audio
