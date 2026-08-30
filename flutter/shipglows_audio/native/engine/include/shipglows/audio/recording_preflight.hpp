#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace shipglows::audio {

struct RecordingPreflightOptions final {
  std::uint64_t minimum_free_bytes = 256ULL * 1024ULL * 1024ULL;
};

struct RecordingPreflightResult final {
  bool ready = false;
  std::uint64_t available_bytes = 0;
  std::string error_code;
};

[[nodiscard]] RecordingPreflightResult recording_preflight(
    const std::filesystem::path& session_directory,
    RecordingPreflightOptions options = {}) noexcept;

}  // namespace shipglows::audio
