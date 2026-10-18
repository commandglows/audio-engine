#pragma once

#include "shipglows/audio/visual_analysis.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace shipglows::audio {

[[nodiscard]] inline std::size_t visual_analysis_record_bytes(
    unsigned feature_mask) {
  switch (feature_mask) {
    case 3: return 2136;
    case 11: return 6492;
    case 5: return 88;
    case 13: return 4444;
    default: throw std::invalid_argument("unsupported analysis mask");
  }
}

[[nodiscard]] inline std::vector<std::uint8_t> encode_visual_analysis_record(
    const VisualAnalysisRecord& record, unsigned feature_mask) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(visual_analysis_record_bytes(feature_mask));
  const auto append_u32 = [&bytes](std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
  };
  const auto append_u64 = [&bytes](std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
      bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
  };
  const auto append_f32 = [&append_u32](float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bits);
  };

  append_u64(record.sequence);
  append_u64(record.center);
  append_u64(record.continuity);
  append_u32(record.valid_frames);
  append_u32((feature_mask & 2) != 0 ? 64u : 0u);
  append_u32(record.flags);
  append_u32(0);
  for (const auto value : record.levels) append_f32(value);
  for (const auto value : record.visual_levels) append_f32(value);
  if ((feature_mask & 2) != 0) {
    for (const auto value : record.envelope) append_f32(value);
    for (const auto value : record.visual_envelope) append_f32(value);
  }
  if ((feature_mask & 8) != 0) {
    for (const auto value : record.spectrum) append_f32(value);
    for (const auto value : record.bands) append_f32(value);
  }
  if (bytes.size() != visual_analysis_record_bytes(feature_mask)) {
    throw std::logic_error("analysis record encoder length mismatch");
  }
  return bytes;
}

}  // namespace shipglows::audio
