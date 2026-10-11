#pragma once
// A0 compatibility names. All processing now runs through the shared engine core.
#include "shipglows/audio/visual_analysis.hpp"

namespace shipglows::audio::a0 {
inline constexpr double pi = shipglows::audio::kVisualAnalysisPi;
using Record = shipglows::audio::VisualAnalysisRecord;
using Stats = shipglows::audio::VisualAnalysisStats;
using Config = shipglows::audio::VisualAnalysisConfig;
using BlockQueue = shipglows::audio::AnalysisBlockQueue;
using Processor = shipglows::audio::VisualAnalysisProcessor;
}  // namespace shipglows::audio::a0
