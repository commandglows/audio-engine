#include "shipglows/audio/recording_preflight.hpp"

#include <array>
#include <fstream>

namespace shipglows::audio {

RecordingPreflightResult recording_preflight(
    const std::filesystem::path& session_directory,
    RecordingPreflightOptions options) noexcept {
  if (session_directory.empty() || !session_directory.is_absolute()) {
    return {.error_code = "session_directory_not_absolute"};
  }
  try {
    if (std::filesystem::exists(session_directory) &&
        !std::filesystem::is_directory(session_directory)) {
      return {.error_code = "session_directory_not_directory"};
    }
    std::filesystem::create_directories(session_directory);
    const auto space = std::filesystem::space(session_directory);
    if (space.available < options.minimum_free_bytes) {
      return {.available_bytes = space.available,
              .error_code = "insufficient_storage"};
    }
    const auto probe_path = session_directory / ".shipglows-write-probe";
    {
      std::ofstream probe(probe_path, std::ios::binary | std::ios::trunc);
      constexpr std::array<char, 4> marker{'S', 'G', 'A', '2'};
      probe.write(marker.data(), marker.size());
      probe.flush();
      if (!probe) {
        return {.available_bytes = space.available,
                .error_code = "session_directory_not_writable"};
      }
    }
    std::filesystem::remove(probe_path);
    return {.ready = true, .available_bytes = space.available, .error_code = {}};
  } catch (...) {
    return {.error_code = "session_directory_not_writable"};
  }
}

}  // namespace shipglows::audio
