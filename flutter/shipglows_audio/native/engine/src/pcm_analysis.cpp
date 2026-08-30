#include "shipglows/audio/pcm_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace shipglows::audio {
namespace {

double normalized_sample(const std::byte* bytes, SampleFormat format) noexcept {
  switch (format) {
    case SampleFormat::int16: {
      std::int16_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      return static_cast<double>(value) / 32768.0;
    }
    case SampleFormat::int24: {
      const auto* value = reinterpret_cast<const unsigned char*>(bytes);
      std::int32_t sample = static_cast<std::int32_t>(value[0]) |
                            (static_cast<std::int32_t>(value[1]) << 8) |
                            (static_cast<std::int32_t>(value[2]) << 16);
      if ((sample & 0x00800000) != 0) {
        sample |= static_cast<std::int32_t>(0xFF000000);
      }
      return static_cast<double>(sample) / 8'388'608.0;
    }
    case SampleFormat::int32: {
      std::int32_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      return static_cast<double>(value) / 2'147'483'648.0;
    }
    case SampleFormat::float32: {
      float value = 0.0F;
      std::memcpy(&value, bytes, sizeof(value));
      return std::isfinite(value)
                 ? std::clamp(static_cast<double>(value), -1.0, 1.0)
                 : 0.0;
    }
  }
  return 0.0;
}

}  // namespace

std::uint64_t count_clipped_samples(std::span<const std::byte> bytes,
                                    AudioFormat format) noexcept {
  const auto sample_bytes = format.bytes_per_sample();
  if (!format.valid() || sample_bytes == 0 || bytes.size() % sample_bytes != 0) {
    return 0;
  }

  std::uint64_t clipped = 0;
  for (std::size_t offset = 0; offset < bytes.size(); offset += sample_bytes) {
    switch (format.sample_format) {
      case SampleFormat::int16: {
        std::int16_t sample = 0;
        std::memcpy(&sample, bytes.data() + offset, sizeof(sample));
        if (sample >= 32760 || sample <= -32760) {
          ++clipped;
        }
        break;
      }
      case SampleFormat::int24: {
        const auto* value = reinterpret_cast<const unsigned char*>(
            bytes.data() + offset);
        std::int32_t sample = static_cast<std::int32_t>(value[0]) |
                              (static_cast<std::int32_t>(value[1]) << 8) |
                              (static_cast<std::int32_t>(value[2]) << 16);
        if ((sample & 0x00800000) != 0) {
          sample |= static_cast<std::int32_t>(0xFF000000);
        }
        if (sample >= 0x007FF000 || sample <= -0x007FF000) {
          ++clipped;
        }
        break;
      }
      case SampleFormat::int32: {
        std::int32_t sample = 0;
        std::memcpy(&sample, bytes.data() + offset, sizeof(sample));
        constexpr auto threshold =
            std::numeric_limits<std::int32_t>::max() - (1 << 20);
        if (sample >= threshold || sample <= -threshold) {
          ++clipped;
        }
        break;
      }
      case SampleFormat::float32: {
        float sample = 0.0F;
        std::memcpy(&sample, bytes.data() + offset, sizeof(sample));
        if (std::isfinite(sample) && std::abs(sample) >= 0.999F) {
          ++clipped;
        }
        break;
      }
    }
  }
  return clipped;
}

AudioLevels analyze_levels(std::span<const std::byte> bytes,
                           AudioFormat format) noexcept {
  const auto sample_bytes = format.bytes_per_sample();
  if (!format.valid() || sample_bytes == 0 || bytes.empty() ||
      bytes.size() % sample_bytes != 0) {
    return {};
  }
  double peak = 0.0;
  double sum_squares = 0.0;
  std::uint64_t count = 0;
  for (std::size_t offset = 0; offset < bytes.size(); offset += sample_bytes) {
    const auto sample = normalized_sample(bytes.data() + offset,
                                          format.sample_format);
    peak = std::max(peak, std::abs(sample));
    sum_squares += sample * sample;
    ++count;
  }
  return {.peak = static_cast<float>(peak),
          .rms = static_cast<float>(std::sqrt(sum_squares / count))};
}

}  // namespace shipglows::audio
