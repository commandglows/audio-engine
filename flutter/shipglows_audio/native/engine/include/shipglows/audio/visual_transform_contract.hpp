#pragma once

#include "shipglows/audio/visual_analysis.hpp"

#include <string>

namespace shipglows::audio {

/// RFC 8785-style canonical v1 transform object shared by live and offline
/// adapters. Keys are fixed and sorted; values are finite and range checked.
[[nodiscard]] std::string visual_transform_config_json(
    const VisualAnalysisConfig& config);
[[nodiscard]] std::string visual_transform_config_digest(
    const VisualAnalysisConfig& config);

}  // namespace shipglows::audio
