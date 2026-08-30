#include "shipglows/audio/pcm_analysis.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace shipglows::audio {

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

}  // namespace shipglows::audio
