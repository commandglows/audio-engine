#pragma once

#include <string_view>

namespace shipglows::audio {

inline constexpr std::string_view kEngineName = "ShipGlows Audio Engine";
inline constexpr std::string_view kEngineVersion = "0.1.0";
inline constexpr std::string_view kSessionSchema =
    "shipglows-audio-session/1";

}  // namespace shipglows::audio
