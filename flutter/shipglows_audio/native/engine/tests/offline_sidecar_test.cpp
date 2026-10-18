#include "shipglows/audio/offline_wav_analysis.hpp"

#include "shipglows/audio/sha256.hpp"
#include "shipglows/audio/visual_analysis_wire.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace shipglows::audio;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

class TemporaryPackage final {
 public:
  TemporaryPackage() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto parent = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      root_ = parent / ("shipglows-sidecar-test-" +
          std::to_string(std::chrono::steady_clock::now()
                             .time_since_epoch().count()) +
          "-" + std::to_string(sequence.fetch_add(1)));
      if (!std::filesystem::exists(root_)) return;
    }
    throw std::runtime_error("could not reserve an isolated test directory");
  }

  ~TemporaryPackage() {
    if (root_.empty() || root_.parent_path() !=
            std::filesystem::temp_directory_path() ||
        root_.filename().string().find("shipglows-sidecar-test-") != 0) {
      return;
    }
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  [[nodiscard]] const std::filesystem::path& root() const { return root_; }
  [[nodiscard]] std::filesystem::path analysis() const {
    return root_ / "analysis-v2";
  }

 private:
  std::filesystem::path root_;
};

OfflineSidecarConfig config_for(const TemporaryPackage& temporary,
                                std::uint64_t sample_count,
                                unsigned mask) {
  return OfflineSidecarConfig{temporary.analysis(), sample_count,
                              offline_sidecar_output_reserve(sample_count, mask),
                              2 * 1024 * 1024, mask, VisualAnalysisConfig{},
                              std::string(40, 'a'), std::string(64, 'b')};
}

VisualAnalysisRecord silence_record(std::uint64_t index,
                                    std::uint32_t valid_frames = 480) {
  VisualAnalysisRecord record;
  record.sequence = index + 1;
  record.center = index * 480;
  record.continuity = 1;
  record.valid_frames = valid_frames;
  return record;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path,
                                    std::size_t maximum = 1024 * 1024) {
  const auto size = std::filesystem::file_size(path);
  expect(size <= maximum, "test reads only bounded sidecar files");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  expect(static_cast<bool>(input), "sidecar file opens for structural check");
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  expect(input.gcount() == static_cast<std::streamsize>(bytes.size()),
         "sidecar file length matches its directory entry");
  return bytes;
}

std::uint32_t get_u32(const std::uint8_t* source) {
  return static_cast<std::uint32_t>(source[0]) |
         (static_cast<std::uint32_t>(source[1]) << 8U) |
         (static_cast<std::uint32_t>(source[2]) << 16U) |
         (static_cast<std::uint32_t>(source[3]) << 24U);
}

std::uint64_t get_u64(const std::uint8_t* source) {
  std::uint64_t value = 0;
  for (unsigned shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(source[shift / 8]) << shift;
  }
  return value;
}

std::uint64_t directory_bytes(const std::filesystem::path& root) {
  std::uint64_t total = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) total += entry.file_size();
  }
  return total;
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::vector<std::uint8_t> silent_wav(std::uint32_t frames) {
  const auto data_bytes = frames * 8;
  std::vector<std::uint8_t> bytes{'R', 'I', 'F', 'F'};
  append_u32(bytes, 36 + data_bytes);
  bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  append_u32(bytes, 16);
  append_u16(bytes, 3);
  append_u16(bytes, 2);
  append_u32(bytes, 48000);
  append_u32(bytes, 384000);
  append_u16(bytes, 8);
  append_u16(bytes, 32);
  bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
  append_u32(bytes, data_bytes);
  bytes.resize(44 + data_bytes, 0);
  return bytes;
}

std::string digest_bytes(const std::vector<std::uint8_t>& bytes) {
  Sha256 digest;
  digest.update(std::as_bytes(std::span(bytes)));
  return Sha256::hex(digest.finalize());
}

void test_single_record_package_and_digests() {
  TemporaryPackage temporary;
  auto config = config_for(temporary, 1, 3);
  OfflineSidecarWriter writer(config);
  expect(writer.working_bytes() < config.max_memory_bytes,
         "writer admits its fixed chunk and page buffers");
  writer.append(silence_record(0, 1));
  const auto result = writer.finish();
  expect(result.feature_count == 1 && result.chunk_count == 1 &&
             result.page_count == 1 && result.output_bytes ==
                 directory_bytes(config.analysis_directory),
         "writer publishes exact one-record package counts and bytes");
  expect(result.reserved_output_bytes == config.max_output_bytes &&
             result.output_bytes < result.reserved_output_bytes,
         "writer enforces the exact reserve while actual files remain smaller");
  expect(result.transform_digest ==
             "74dc6923a924808157182ff39fcd281ef9560184235e4b48d084341f47784ef5",
         "sidecar uses the shared transform digest");
  expect(sha256_file(config.analysis_directory / "pages.sgai") ==
             result.page_index_sha256 &&
             sha256_file(config.analysis_directory / "metadata.json") ==
                 result.metadata_sha256,
         "index and metadata digests cover the bytes written to disk");

  const auto chunk_path = config.analysis_directory /
                          "chunks/chunk-0000000000000000.sgav";
  const auto chunk = read_file(chunk_path);
  expect(chunk.size() == 64 + 2136 &&
             std::string(chunk.begin(), chunk.begin() + 8) == "SGAVC002" &&
             get_u32(chunk.data() + 8) == 2 &&
             get_u32(chunk.data() + 12) == 2136 &&
             get_u64(chunk.data() + 16) == 0 &&
             get_u32(chunk.data() + 24) == 1 &&
             get_u32(chunk.data() + 28) == 3 &&
             get_u32(chunk.data() + 32) == 1 &&
             get_u64(chunk.data() + 40) == 0 &&
             get_u64(chunk.data() + 48) == 2136,
         "chunk header follows the fixed v2 little-endian layout");
  const auto expected_record = encode_visual_analysis_record(silence_record(0, 1), 3);
  expect(std::equal(expected_record.begin(), expected_record.end(), chunk.begin() + 64),
         "chunk payload reuses the live compact record layout");

  const auto page = read_file(config.analysis_directory /
                              "pages/page-0000000000000000.sgip");
  const auto index = read_file(config.analysis_directory / "pages.sgai");
  expect(page.size() == 64 + 128 &&
             std::string(page.begin(), page.begin() + 8) == "SGAVP002" &&
             get_u64(page.data() + 16) == 0 && get_u32(page.data() + 24) == 1,
         "page header carries the first chunk descriptor");
  expect(index.size() == 64 + 128 &&
             std::string(index.begin(), index.begin() + 8) == "SGAVI002" &&
             get_u32(index.data() + 24) == 1 &&
             get_u64(index.data() + 32) == 128,
         "fixed page index stores one bounded descriptor");
  const auto metadata_bytes = read_file(config.analysis_directory / "metadata.json",
                                        65536);
  const std::string metadata(metadata_bytes.begin(), metadata_bytes.end());
  expect(metadata.find("\"sample_count\":1") != std::string::npos &&
             metadata.find("\"feature_count\":1") != std::string::npos &&
             metadata.find("\"page_index_sha256\":\"") != std::string::npos &&
             metadata.find("first_chunk_index") == std::string::npos,
         "root metadata is bounded and does not inline page descriptors");

  OfflineSidecarReader reader(config);
  const auto record = reader.read_record_bytes(0);
  expect(reader.result().cache_key == result.cache_key &&
             reader.result().output_bytes == result.output_bytes &&
             record == expected_record,
         "reader validates the package and returns random-access compact records");

  const auto index_path = config.analysis_directory / "pages.sgai";
  const auto original_index = read_file(index_path);
  {
    std::fstream changed_index(index_path, std::ios::binary | std::ios::in |
                                               std::ios::out);
    changed_index.seekp(64);
    const char changed = static_cast<char>(original_index[64] ^ 1U);
    changed_index.write(&changed, 1);
  }
  bool rejected_post_admission_change = false;
  try {
    (void)reader.read_record_bytes(0);
  } catch (const OfflineAnalysisError& error) {
    rejected_post_admission_change = error.code() ==
                       OfflineAnalysisErrorCode::output_validation_failed;
  }
  expect(rejected_post_admission_change,
         "reader rechecks index identity before later random-access reads");
  {
    std::fstream restored_index(index_path, std::ios::binary | std::ios::in |
                                                std::ios::out);
    restored_index.seekp(64);
    restored_index.write(reinterpret_cast<const char*>(original_index.data() + 64), 1);
  }
  const auto unexpected_path = config.analysis_directory / "unexpected.bin";
  {
    std::ofstream unexpected(unexpected_path, std::ios::binary);
    unexpected.put('x');
  }
  bool rejected_unexpected_file = false;
  try {
    OfflineSidecarReader extra_file(config);
  } catch (const OfflineAnalysisError& error) {
    rejected_unexpected_file = error.code() ==
                               OfflineAnalysisErrorCode::output_validation_failed;
  }
  std::filesystem::remove(unexpected_path);
  expect(rejected_unexpected_file,
         "reader rejects unrecognized package entries instead of ignoring them");

  {
    std::fstream damaged(chunk_path, std::ios::binary | std::ios::in |
                                          std::ios::out);
    damaged.seekp(64 + 40);
    const char changed = 1;
    damaged.write(&changed, 1);
  }
  bool rejected_post_admission_chunk_change = false;
  try {
    (void)reader.read_record_bytes(0);
  } catch (const OfflineAnalysisError& error) {
    rejected_post_admission_chunk_change = error.code() ==
                       OfflineAnalysisErrorCode::output_validation_failed;
  }
  expect(rejected_post_admission_chunk_change,
         "reader rechecks chunk integrity before later random-access reads");
  bool rejected_corrupt = false;
  try {
    OfflineSidecarReader corrupted(config);
  } catch (const OfflineAnalysisError& error) {
    rejected_corrupt = error.code() ==
                       OfflineAnalysisErrorCode::output_validation_failed;
  }
  expect(rejected_corrupt,
         "reader rejects a record mutation that no longer matches the chunk digest");
}

void test_page_boundary_and_incremental_memory() {
  constexpr std::uint64_t kRecords = 25700;
  TemporaryPackage temporary;
  auto config = config_for(temporary, kRecords * 480, 5);
  OfflineSidecarWriter writer(config);
  const auto writer_bytes = writer.working_bytes();
  for (std::uint64_t index = 0; index < kRecords; ++index) {
    writer.append(silence_record(index));
  }
  const auto result = writer.finish();
  expect(result.feature_count == kRecords && result.chunk_count == 257 &&
             result.page_count == 2,
         "257 chunks roll into two descriptor pages without a duration-sized index");
  expect(result.working_bytes == writer_bytes &&
             result.output_bytes == directory_bytes(config.analysis_directory),
         "writer keeps a fixed reported working set across all emitted records");
  const auto first_page = read_file(config.analysis_directory /
                                    "pages/page-0000000000000000.sgip");
  const auto second_page = read_file(config.analysis_directory /
                                     "pages/page-0000000000000001.sgip");
  const auto index = read_file(config.analysis_directory / "pages.sgai");
  expect(first_page.size() == 64 + 256 * 128 &&
             get_u64(first_page.data() + 16) == 0 &&
             get_u32(first_page.data() + 24) == 256 &&
             second_page.size() == 64 + 128 &&
             get_u64(second_page.data() + 16) == 256 &&
             get_u32(second_page.data() + 24) == 1 &&
             index.size() == 64 + 2 * 128 &&
             get_u32(index.data() + 24) == 2,
         "page and index lengths close exactly at descriptor boundaries");
  OfflineSidecarReader reader(config);
  const auto last_record = reader.read_record_bytes(kRecords - 1);
  expect(last_record == encode_visual_analysis_record(
             silence_record(kRecords - 1), 5),
         "reader crosses the bounded page index to retrieve a late record");
}

void test_reader_all_feature_masks() {
  for (const unsigned mask : {3U, 5U, 11U, 13U}) {
    TemporaryPackage temporary;
    auto config = config_for(temporary, 480, mask);
    OfflineSidecarWriter writer(config);
    writer.append(silence_record(0));
    (void)writer.finish();
    OfflineSidecarReader reader(config);
    expect(reader.read_record_bytes(0) ==
               encode_visual_analysis_record(silence_record(0), mask),
           "reader validates profile-specific record layouts and offsets");
    if (mask == 11) {
      auto too_small = config;
      too_small.max_memory_bytes = 1024 * 1024;
      bool rejected_memory = false;
      try {
        OfflineSidecarReader limited(too_small);
      } catch (const OfflineAnalysisError& error) {
        rejected_memory = error.code() ==
                          OfflineAnalysisErrorCode::resource_limit_exceeded;
      }
      expect(rejected_memory,
             "reader rejects a working-set estimate above the memory cap");
    }
  }
}

void test_empty_package_and_preflight_rejections() {
  TemporaryPackage empty_package;
  auto empty_config = config_for(empty_package, 0, 5);
  OfflineSidecarWriter empty_writer(empty_config);
  const auto empty_result = empty_writer.finish();
  expect(empty_result.feature_count == 0 && empty_result.chunk_count == 0 &&
             empty_result.page_count == 0 &&
             std::filesystem::file_size(empty_config.analysis_directory / "pages.sgai") == 64,
         "empty source has a valid empty fixed index and no fake feature records");
  OfflineSidecarReader empty_reader(empty_config);
  expect(empty_reader.result().feature_count == 0 &&
             empty_reader.result().output_bytes == empty_result.output_bytes,
         "reader admits a valid empty package without allocating an index");

  TemporaryPackage rejected_package;
  auto rejected = config_for(rejected_package, 1, 5);
  --rejected.max_output_bytes;
  bool rejected_reserve = false;
  try {
    OfflineSidecarWriter writer(rejected);
  } catch (const OfflineAnalysisError& error) {
    rejected_reserve = error.code() ==
                       OfflineAnalysisErrorCode::resource_limit_exceeded;
  }
  expect(rejected_reserve && !std::filesystem::exists(rejected.analysis_directory),
         "short output budget is rejected before creating package files");

  bool rejected_overflow = false;
  try {
    (void)offline_sidecar_output_reserve(
        std::numeric_limits<std::uint64_t>::max(), 11);
  } catch (const OfflineAnalysisError& error) {
    rejected_overflow = error.code() ==
                        OfflineAnalysisErrorCode::resource_limit_exceeded;
  }
  expect(rejected_overflow, "sidecar reserve rejects uint64 arithmetic overflow");
}

void test_end_to_end_wav_to_sidecar_composition() {
  constexpr std::uint64_t frames = 777;
  const auto bytes = silent_wav(static_cast<std::uint32_t>(frames));
  const std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::istringstream input(content, std::ios::binary | std::ios::in);
  TemporaryPackage temporary;
  auto config = config_for(temporary, frames, 5);
  config.max_memory_bytes = 4 * 1024 * 1024;
  config.input_sha256 = digest_bytes(bytes);
  OfflineWavRequest request{frames, 48000 * 60, config.max_memory_bytes, 257,
                            digest_bytes(bytes)};
  const auto result = analyze_wav_to_sidecar(
      input, 5, VisualAnalysisConfig{}, request, config);
  expect(result.analysis.feature_count == 2 &&
             result.sidecar.feature_count == 2 &&
             result.sidecar.chunk_count == 1 &&
             result.sidecar.output_bytes == directory_bytes(config.analysis_directory),
         "WAV streaming analysis composes directly into the paged sidecar writer");
  const auto chunk = read_file(config.analysis_directory /
                               "chunks/chunk-0000000000000000.sgav");
  expect(get_u32(chunk.data() + 24) == 2 &&
             get_u32(chunk.data() + 12) == 88 &&
             get_u32(chunk.data() + 64 + 24) == 480 &&
             get_u32(chunk.data() + 64 + 88 + 24) == 297,
         "end-to-end sidecar retains exact full and partial final hop lengths");
}

}  // namespace

int main() {
  test_single_record_package_and_digests();
  test_page_boundary_and_incremental_memory();
  test_reader_all_feature_masks();
  test_empty_package_and_preflight_rejections();
  test_end_to_end_wav_to_sidecar_composition();
}
