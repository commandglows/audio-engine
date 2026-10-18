#pragma once

#include "shipglows/audio/offline_wav_analysis.hpp"

#include <filesystem>
#include <functional>

namespace shipglows::audio::offline_job {

/// Executes one private local job under a caller-authorized job root. The
/// manifest and every input/output path are relative to that root. The result
/// directory becomes visible only after the sidecar reader validates it.
[[nodiscard]] OfflinePackageResult run(
    const std::filesystem::path& job_root,
    const std::filesystem::path& manifest_relative_path,
    std::function<bool()> cancellation_requested = {});

}  // namespace shipglows::audio::offline_job
