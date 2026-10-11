#include "offline_job.hpp"

#include "shipglows/audio/sha256.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace shipglows::audio;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

class JobRoot final {
 public:
  JobRoot() {
    const auto parent = std::filesystem::temp_directory_path();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      path_ = parent / ("shipglows-offline-job-test-" +
          std::to_string(std::chrono::steady_clock::now()
                             .time_since_epoch().count()) +
          "-" + std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(path_, error)) return;
    }
    throw std::runtime_error("could not reserve an isolated job root");
  }

  ~JobRoot() {
    if (path_.empty() || path_.parent_path() !=
            std::filesystem::temp_directory_path() ||
        path_.filename().string().find("shipglows-offline-job-test-") != 0) {
      return;
    }
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void put_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::vector<std::uint8_t> make_wav(std::uint32_t frames) {
  std::vector<std::uint8_t> bytes;
  const auto data_bytes = frames * 8;
  bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
  put_u32(bytes, 36 + data_bytes);
  bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  put_u32(bytes, 16);
  put_u16(bytes, 3);
  put_u16(bytes, 2);
  put_u32(bytes, 48000);
  put_u32(bytes, 384000);
  put_u16(bytes, 8);
  put_u16(bytes, 32);
  bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
  put_u32(bytes, data_bytes);
  bytes.resize(44 + data_bytes, 0);
  return bytes;
}

std::string digest(const std::vector<std::uint8_t>& bytes) {
  Sha256 hash;
  hash.update(std::as_bytes(std::span(bytes)));
  return Sha256::hex(hash.finalize());
}

std::string manifest_json(const std::string& job_id,
                          const std::string& input_path,
                          const std::string& input_digest,
                          std::uint64_t frames,
                          std::uint64_t runtime_ms = 60000) {
  return "{\"schema_version\":1,\"job_id\":\"" + job_id +
      "\",\"input_path\":\"" + input_path +
      "\",\"sample_count\":" + std::to_string(frames) +
      ",\"max_duration_frames\":2880000,\"max_memory_bytes\":4194304"
      ",\"max_output_bytes\":" + std::to_string(
          offline_sidecar_output_reserve(frames, 5)) +
      ",\"max_runtime_ms\":" + std::to_string(runtime_ms) +
      ",\"block_frames\":257,"
      "\"feature_mask\":5,\"input_sha256\":\"" +
      input_digest + "\",\"engine_revision\":\"" + std::string(40, 'a') +
      "\",\"sensitivity_db\":0,\"amplitude_gamma\":0.8,"
      "\"attack_ms\":20,\"release_ms\":180,\"spectrum_floor_db\":-80,"
      "\"spectrum_ceiling_db\":0}";
}

void write_manifest(const JobRoot& root, const std::string& contents) {
  std::ofstream manifest(root.path() / "job.json", std::ios::binary);
  manifest.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  expect(static_cast<bool>(manifest), "test manifest is written");
}

std::vector<std::filesystem::path> entries(const std::filesystem::path& path) {
  std::vector<std::filesystem::path> result;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    result.push_back(entry.path());
  }
  return result;
}

void create_input(const JobRoot& root, const std::vector<std::uint8_t>& wav) {
  std::ofstream input(root.path() / "tone.wav", std::ios::binary);
  input.write(reinterpret_cast<const char*>(wav.data()),
              static_cast<std::streamsize>(wav.size()));
  expect(static_cast<bool>(input), "test WAV is written");
}

void test_job_is_published_only_after_reader_validation() {
  constexpr std::uint64_t frames = 777;
  const auto wav = make_wav(static_cast<std::uint32_t>(frames));
  JobRoot root;
  create_input(root, wav);
  write_manifest(root, manifest_json("demo", "tone.wav", digest(wav), frames));

  const auto package = offline_job::run(root.path(), "job.json");
  const auto published = root.path() / "jobs" / "demo";
  expect(std::filesystem::is_directory(published) &&
             std::filesystem::is_regular_file(published / "result.json") &&
             std::filesystem::is_regular_file(
                 published / "analysis-v2" / "metadata.json"),
         "complete package and summary appear at the final job path");
  expect(package.sidecar.feature_count == 2 &&
             entries(root.path() / "jobs").size() == 1,
         "publication leaves no sibling staging directory");

  OfflineSidecarConfig expected{
      published / "analysis-v2", frames,
      offline_sidecar_output_reserve(frames, 5), 4 * 1024 * 1024, 5,
      VisualAnalysisConfig{}, std::string(40, 'a'), digest(wav)};
  OfflineSidecarReader reader(expected);
  expect(reader.result().cache_key == package.sidecar.cache_key,
         "published package can be reopened using the admitted identity");
  std::ifstream result(published / "result.json", std::ios::binary);
  const std::string summary((std::istreambuf_iterator<char>(result)), {});
  expect(summary.find("tone.wav") == std::string::npos &&
             summary.find("cache_key") != std::string::npos,
         "result summary exposes machine identity without local source paths");
}

void test_cancellation_removes_only_staging_output() {
  constexpr std::uint64_t frames = 777;
  const auto wav = make_wav(static_cast<std::uint32_t>(frames));
  JobRoot root;
  create_input(root, wav);
  write_manifest(root, manifest_json("cancelled", "tone.wav", digest(wav), frames));
  bool caught_cancel = false;
  try {
    (void)offline_job::run(root.path(), "job.json", [] { return true; });
  } catch (const OfflineAnalysisError& error) {
    caught_cancel = error.code() == OfflineAnalysisErrorCode::cancelled;
  }
  expect(caught_cancel, "runner returns the typed cancellation result");
  expect(std::filesystem::is_directory(root.path() / "jobs") &&
             entries(root.path() / "jobs").empty() &&
             !std::filesystem::exists(root.path() / "jobs" / "cancelled"),
         "cancelled job cleans its own private staging directory");
}

void test_wall_time_limit_cleans_staging() {
  constexpr std::uint64_t frames = 777;
  const auto wav = make_wav(static_cast<std::uint32_t>(frames));
  JobRoot root;
  create_input(root, wav);
  write_manifest(root, manifest_json("timeout", "tone.wav", digest(wav),
                                     frames, 1));
  bool caught_timeout = false;
  try {
    (void)offline_job::run(root.path(), "job.json", [] {
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      return false;
    });
  } catch (const OfflineAnalysisError& error) {
    caught_timeout = error.code() == OfflineAnalysisErrorCode::timed_out;
  }
  expect(caught_timeout && entries(root.path() / "jobs").empty(),
         "wall-time expiry is typed and removes only the private staging job");
}

void test_path_traversal_duplicate_fields_and_existing_target_reject() {
  constexpr std::uint64_t frames = 777;
  const auto wav = make_wav(static_cast<std::uint32_t>(frames));
  const auto good = manifest_json("demo", "tone.wav", digest(wav), frames);

  JobRoot traversal;
  create_input(traversal, wav);
  auto unsafe = manifest_json("demo", "../tone.wav", digest(wav), frames);
  write_manifest(traversal, unsafe);
  bool rejected_path = false;
  try {
    (void)offline_job::run(traversal.path(), "job.json");
  } catch (const OfflineAnalysisError& error) {
    rejected_path = error.code() == OfflineAnalysisErrorCode::invalid_media;
  }
  expect(rejected_path && !std::filesystem::exists(traversal.path() / "jobs"),
         "path traversal is rejected before output staging begins");

  JobRoot duplicate;
  create_input(duplicate, wav);
  write_manifest(duplicate, good.substr(0, good.size() - 1) +
      ",\"job_id\":\"second\"}");
  bool rejected_duplicate = false;
  try {
    (void)offline_job::run(duplicate.path(), "job.json");
  } catch (const OfflineAnalysisError& error) {
    rejected_duplicate = error.code() == OfflineAnalysisErrorCode::invalid_media;
  }
  expect(rejected_duplicate && !std::filesystem::exists(duplicate.path() / "jobs"),
         "duplicate manifest keys fail before any output is created");

  JobRoot existing;
  create_input(existing, wav);
  write_manifest(existing, good);
  std::filesystem::create_directories(existing.path() / "jobs" / "demo");
  {
    std::ofstream sentinel(existing.path() / "jobs" / "demo" / "sentinel.txt");
    sentinel << "keep";
  }
  bool rejected_target = false;
  try {
    (void)offline_job::run(existing.path(), "job.json");
  } catch (const OfflineAnalysisError& error) {
    rejected_target = error.code() ==
                      OfflineAnalysisErrorCode::output_validation_failed;
  }
  std::ifstream sentinel(existing.path() / "jobs" / "demo" / "sentinel.txt");
  std::string value;
  sentinel >> value;
  expect(rejected_target && value == "keep" &&
             entries(existing.path() / "jobs").size() == 1,
         "existing output is never replaced or removed");
}

}  // namespace

int main() {
  test_job_is_published_only_after_reader_validation();
  test_cancellation_removes_only_staging_output();
  test_wall_time_limit_cleans_staging();
  test_path_traversal_duplicate_fields_and_existing_target_reject();
}
