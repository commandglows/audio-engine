#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "shipglows/audio/audio_types.hpp"

namespace shipglows::audio {

[[nodiscard]] std::uint64_t count_clipped_samples(
    std::span<const std::byte> bytes, AudioFormat format) noexcept;

}  // namespace shipglows::audio
