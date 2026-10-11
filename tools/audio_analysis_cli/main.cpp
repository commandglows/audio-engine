#include "offline_job.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

namespace {
static_assert(std::atomic_bool::is_always_lock_free,
              "CLI cancellation flag must be signal-safe on this target.");
std::atomic_bool cancelled{false};

void request_cancel(int) { cancelled.store(true, std::memory_order_relaxed); }

const char* error_name(shipglows::audio::OfflineAnalysisErrorCode code) {
  using shipglows::audio::OfflineAnalysisErrorCode;
  switch (code) {
    case OfflineAnalysisErrorCode::invalid_media: return "invalid_manifest_or_media";
    case OfflineAnalysisErrorCode::unsupported_media: return "unsupported_media";
    case OfflineAnalysisErrorCode::invalid_pcm: return "invalid_pcm";
    case OfflineAnalysisErrorCode::resource_limit_exceeded: return "resource_limit";
    case OfflineAnalysisErrorCode::input_changed: return "input_changed";
    case OfflineAnalysisErrorCode::analysis_failed: return "analysis_failed";
    case OfflineAnalysisErrorCode::output_validation_failed: return "output_failed";
    case OfflineAnalysisErrorCode::cancelled: return "cancelled";
    case OfflineAnalysisErrorCode::timed_out: return "time_limit";
  }
  return "unknown";
}

int exit_code(shipglows::audio::OfflineAnalysisErrorCode code) {
  using shipglows::audio::OfflineAnalysisErrorCode;
  if (code == OfflineAnalysisErrorCode::cancelled) return 130;
  if (code == OfflineAnalysisErrorCode::timed_out) return 124;
  if (code == OfflineAnalysisErrorCode::resource_limit_exceeded) return 4;
  if (code == OfflineAnalysisErrorCode::output_validation_failed) return 5;
  if (code == OfflineAnalysisErrorCode::input_changed) return 6;
  if (code == OfflineAnalysisErrorCode::analysis_failed) return 7;
  if (code == OfflineAnalysisErrorCode::invalid_pcm) return 8;
  if (code == OfflineAnalysisErrorCode::unsupported_media) return 9;
  return 3;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5 || std::string(argv[1]) != "--job-root" ||
      std::string(argv[3]) != "--manifest") {
    std::cerr << "usage: shipglows_audio_analysis_cli --job-root DIR "
                 "--manifest RELATIVE_JSON\n";
    return 2;
  }
  std::signal(SIGINT, request_cancel);
  std::signal(SIGTERM, request_cancel);
  try {
    const auto package = shipglows::audio::offline_job::run(
        argv[2], argv[4], [] { return cancelled.load(std::memory_order_relaxed); });
    std::cout << "{\"cache_key\":\"" << package.sidecar.cache_key
              << "\",\"chunk_count\":" << package.sidecar.chunk_count
              << ",\"feature_count\":" << package.sidecar.feature_count
              << ",\"output_bytes\":" << package.sidecar.output_bytes
              << ",\"page_count\":" << package.sidecar.page_count
              << ",\"sample_count\":" << package.sidecar.sample_count
              << ",\"status\":\"published\"}\n";
    return 0;
  } catch (const shipglows::audio::OfflineAnalysisError& error) {
    std::cerr << "{\"error\":\"" << error_name(error.code())
              << "\",\"status\":\"failed\"}\n";
    return exit_code(error.code());
  } catch (...) {
    std::cerr << "{\"error\":\"internal\",\"status\":\"failed\"}\n";
    return 10;
  }
}
