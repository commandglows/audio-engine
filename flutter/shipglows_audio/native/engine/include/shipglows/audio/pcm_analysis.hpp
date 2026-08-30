#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "shipglows/audio/audio_types.hpp"

namespace shipglows::audio {

struct AudioLevels final {
  float peak = 0.0F;
  float rms = 0.0F;
};

[[nodiscard]] std::uint64_t count_clipped_samples(
    std::span<const std::byte> bytes, AudioFormat format) noexcept;

[[nodiscard]] AudioLevels analyze_levels(
    std::span<const std::byte> bytes, AudioFormat format) noexcept;

}  // namespace shipglows::audio
