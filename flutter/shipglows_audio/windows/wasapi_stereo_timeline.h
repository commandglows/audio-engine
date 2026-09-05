#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
namespace shipglows_audio {
// Bounded stereo accumulation on a shared sample clock. Late samples are
// discarded rather than shifted (which would progressively desynchronize).
inline bool AddStereoPacket(std::span<float> timeline, uint64_t consumed,
    int64_t first, std::span<const float> packet, float gain) {
  const auto capacity = timeline.size() / 2;
  for (size_t frame = 0; frame < packet.size() / 2; ++frame) {
    const auto destination = first + static_cast<int64_t>(frame);
    if (destination < static_cast<int64_t>(consumed)) continue;
    if (destination >= static_cast<int64_t>(consumed + capacity)) return false;
    for (size_t channel = 0; channel < 2; ++channel)
      timeline[(static_cast<uint64_t>(destination) % capacity) * 2 + channel] +=
          packet[frame * 2 + channel] * gain;
  }
  return true;
}
}
