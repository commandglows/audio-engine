#pragma once

#include "shipglows/audio/sha256.hpp"
#include "shipglows/audio/visual_analysis.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <istream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace shipglows::audio {

enum class OfflineAnalysisErrorCode {
  invalid_media,
  unsupported_media,
  invalid_pcm,
  resource_limit_exceeded,
  input_changed,
  analysis_failed,
  output_validation_failed,
  cancelled,
  timed_out,
};

class OfflineAnalysisError final : public std::runtime_error {
 public:
  OfflineAnalysisError(OfflineAnalysisErrorCode code, const char* message);
  [[nodiscard]] OfflineAnalysisErrorCode code() const noexcept { return code_; }

 private:
  OfflineAnalysisErrorCode code_;
};

struct OfflineWavRequest final {
  std::uint64_t sample_count = 0;
  std::uint64_t max_duration_frames = 0;
  std::uint64_t max_memory_bytes = 0;
  std::uint32_t block_frames = 4096;
  std::string input_sha256;
  std::function<bool()> cancellation_requested;
};

struct OfflineWavResult final {
  std::uint64_t sample_count = 0;
  std::uint64_t feature_count = 0;
  std::uint64_t estimated_working_bytes = 0;
  std::uint32_t record_bytes = 0;
  std::string input_sha256;
  VisualAnalysisStats stats{};
};

struct OfflineSidecarConfig final {
  std::filesystem::path analysis_directory;
  std::uint64_t sample_count = 0;
  std::uint64_t max_output_bytes = 0;
  std::uint64_t max_memory_bytes = 0;
  unsigned feature_mask = 0;
  VisualAnalysisConfig transform{};
  std::string engine_revision;
  std::string input_sha256;
};

struct OfflineSidecarResult final {
  std::uint64_t sample_count = 0;
  std::uint64_t feature_count = 0;
  std::uint64_t chunk_count = 0;
  std::uint64_t page_count = 0;
  std::uint64_t output_bytes = 0;
  std::uint64_t reserved_output_bytes = 0;
  std::uint64_t working_bytes = 0;
  std::string transform_digest;
  std::string cache_key;
  std::string page_index_sha256;
  std::string metadata_sha256;
};

struct OfflinePackageResult final {
  OfflineWavResult analysis;
  OfflineSidecarResult sidecar;
};

/// Read-only random-access view over a fully validated analysis-v2 directory.
/// Admission requires the source/cache identity expected by the caller. The
/// reader validates all metadata, page/chunk hashes and record invariants
/// without retaining the complete index or the complete record set.
class OfflineSidecarReader final {
 public:
  explicit OfflineSidecarReader(
      OfflineSidecarConfig expected,
      std::function<bool()> cancellation_requested = {});
  [[nodiscard]] const OfflineSidecarResult& result() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t> read_record_bytes(
      std::uint64_t record_index) const;
  [[nodiscard]] std::uint64_t working_bytes() const noexcept;

 private:
  OfflineSidecarConfig expected_;
  OfflineSidecarResult result_;
  std::function<bool()> cancellation_requested_;
};

[[nodiscard]] std::uint64_t offline_wav_analysis_working_bytes(
    std::uint32_t block_frames);

[[nodiscard]] std::uint64_t offline_sidecar_working_bytes(
    unsigned feature_mask);

/// Computes the exact v2 reserve before any package files are created.
[[nodiscard]] std::uint64_t offline_sidecar_output_reserve(
    std::uint64_t sample_count, unsigned feature_mask);

/// Connects the validated WAV stream, shared analysis core and sidecar writer.
/// The memory cap is for the combined analyzer and writer working sets.
[[nodiscard]] OfflinePackageResult analyze_wav_to_sidecar(
    std::istream& input, unsigned feature_mask,
    const VisualAnalysisConfig& transform, OfflineWavRequest wav_request,
    OfflineSidecarConfig sidecar_config);

/// Streaming writer for one private analysis-v2 directory. The caller must
/// provide a new, private staging directory and atomically publish its parent
/// only after analysis and this writer both finish successfully.
class OfflineSidecarWriter final {
 public:
  explicit OfflineSidecarWriter(OfflineSidecarConfig config);
  ~OfflineSidecarWriter();
  OfflineSidecarWriter(const OfflineSidecarWriter&) = delete;
  OfflineSidecarWriter& operator=(const OfflineSidecarWriter&) = delete;

  void append(const VisualAnalysisRecord& record);
  [[nodiscard]] OfflineSidecarResult finish();
  [[nodiscard]] std::uint64_t working_bytes() const noexcept;

 private:
  void flush_chunk();
  void flush_page();
  void add_output_bytes(std::uint64_t bytes);

  OfflineSidecarConfig config_;
  std::uint64_t feature_count_ = 0;
  std::uint64_t chunk_count_ = 0;
  std::uint64_t page_count_ = 0;
  std::uint64_t reserve_bytes_ = 0;
  std::uint64_t output_bytes_ = 0;
  std::uint64_t records_written_ = 0;
  std::uint64_t chunks_written_ = 0;
  std::uint64_t pages_written_ = 0;
  std::uint64_t page_first_record_ = 0;
  std::uint64_t first_center_frame_ = 0;
  std::uint32_t records_in_chunk_ = 0;
  std::uint32_t descriptors_in_page_ = 0;
  std::size_t chunk_payload_bytes_ = 0;
  std::uint32_t record_bytes_ = 0;
  std::string transform_json_;
  std::string transform_digest_;
  std::string cache_key_;
  std::unique_ptr<std::uint8_t[]> chunk_payload_;
  std::array<std::array<std::uint8_t, 128>, 256> page_descriptors_{};
  std::ofstream page_index_stream_;
  Sha256 page_index_hash_;
  bool finished_ = false;
};

/// Analyzes an admitted IEEE-float32, stereo, 48 kHz RIFF/WAVE stream.
/// The caller owns path authorization and output publication. Memory use is
/// bounded by the request block size and analyzer context, independent of file
/// duration. The input is read twice; a digest change between passes fails.
[[nodiscard]] OfflineWavResult analyze_offline_wav(
    std::istream& input, unsigned feature_mask,
    const VisualAnalysisConfig& transform, const OfflineWavRequest& request,
    const std::function<void(const VisualAnalysisRecord&)>& record_sink);

}  // namespace shipglows::audio
