#include "offline_job.hpp"

#include "shipglows/audio/visual_transform_contract.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace shipglows::audio::offline_job {
namespace {

[[noreturn]] void fail(OfflineAnalysisErrorCode code, const char* message) {
  throw OfflineAnalysisError(code, message);
}

struct Manifest final {
  std::string job_id;
  std::string input_path;
  std::uint64_t sample_count = 0;
  std::uint64_t max_duration_frames = 0;
  std::uint64_t max_memory_bytes = 0;
  std::uint64_t max_output_bytes = 0;
  std::uint64_t max_runtime_ms = 0;
  std::uint32_t block_frames = 0;
  unsigned feature_mask = 0;
  std::string input_sha256;
  std::string engine_revision;
  VisualAnalysisConfig transform{};
};

class FlatJson final {
 public:
  explicit FlatJson(std::string_view source) : source_(source) {}

  std::map<std::string, std::string> parse() {
    skip_space();
    expect('{');
    skip_space();
    std::map<std::string, std::string> values;
    if (consume('}')) return values;
    for (;;) {
      const auto key = parse_string();
      skip_space();
      expect(':');
      skip_space();
      const auto value = parse_value();
      if (!values.emplace(key, value).second) {
        fail(OfflineAnalysisErrorCode::invalid_media,
             "Job manifest contains a duplicate field.");
      }
      skip_space();
      if (consume('}')) break;
      expect(',');
      skip_space();
    }
    skip_space();
    if (position_ != source_.size()) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job manifest has trailing content.");
    }
    return values;
  }

 private:
  void skip_space() {
    while (position_ < source_.size() &&
           (source_[position_] == ' ' || source_[position_] == '\n' ||
            source_[position_] == '\r' || source_[position_] == '\t')) {
      ++position_;
    }
  }

  bool consume(char value) {
    if (position_ < source_.size() && source_[position_] == value) {
      ++position_;
      return true;
    }
    return false;
  }

  void expect(char value) {
    if (!consume(value)) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job manifest syntax is invalid.");
    }
  }

  std::string parse_string() {
    expect('"');
    std::string value;
    while (position_ < source_.size() && source_[position_] != '"') {
      const auto byte = static_cast<unsigned char>(source_[position_++]);
      if (byte < 0x20 || byte > 0x7e || byte == '\\') {
        fail(OfflineAnalysisErrorCode::invalid_media,
             "Job manifest strings must use the closed printable ASCII subset.");
      }
      value.push_back(static_cast<char>(byte));
    }
    expect('"');
    return value;
  }

  std::string parse_value() {
    if (position_ < source_.size() && source_[position_] == '"') {
      return std::string("\"") + parse_string() + "\"";
    }
    const auto begin = position_;
    while (position_ < source_.size() && source_[position_] != ',' &&
           source_[position_] != '}' && source_[position_] != ' ' &&
           source_[position_] != '\n' && source_[position_] != '\r' &&
           source_[position_] != '\t') {
      ++position_;
    }
    if (position_ == begin) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job manifest field is missing a value.");
    }
    return std::string(source_.substr(begin, position_ - begin));
  }

  std::string_view source_;
  std::size_t position_ = 0;
};

std::string string_field(const std::map<std::string, std::string>& fields,
                         const char* name) {
  const auto found = fields.find(name);
  if (found == fields.end() || found->second.size() < 2 ||
      found->second.front() != '"' || found->second.back() != '"') {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest is missing a required string field.");
  }
  return found->second.substr(1, found->second.size() - 2);
}

std::uint64_t integer_field(const std::map<std::string, std::string>& fields,
                            const char* name) {
  const auto found = fields.find(name);
  std::uint64_t value = 0;
  if (found == fields.end() || found->second.empty() ||
      (found->second.size() > 1 && found->second.front() == '0')) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest is missing a canonical integer field.");
  }
  const auto [end, error] = std::from_chars(
      found->second.data(), found->second.data() + found->second.size(), value);
  if (error != std::errc{} || end != found->second.data() + found->second.size()) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest integer field is invalid or overflows.");
  }
  return value;
}

double number_field(const std::map<std::string, std::string>& fields,
                    const char* name) {
  const auto found = fields.find(name);
  double value = 0;
  if (found == fields.end() || found->second.empty()) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest is missing a transform number.");
  }
  const auto [end, error] = std::from_chars(
      found->second.data(), found->second.data() + found->second.size(), value,
      std::chars_format::general);
  if (error != std::errc{} || end != found->second.data() + found->second.size() ||
      !std::isfinite(value)) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest transform number is invalid or non-finite.");
  }
  return value;
}

Manifest parse_manifest(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest must be a regular file without links.");
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > 16 * 1024) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Job manifest exceeds its 16 KiB admission limit.");
  }
  std::ifstream input(path, std::ios::binary);
  std::string json(static_cast<std::size_t>(size), '\0');
  if (!input || !input.read(json.data(), static_cast<std::streamsize>(size))) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest could not be read completely.");
  }
  const auto fields = FlatJson(json).parse();
  static const std::set<std::string> expected{
      "schema_version", "job_id", "input_path", "sample_count",
      "max_duration_frames", "max_memory_bytes", "max_output_bytes",
      "max_runtime_ms", "block_frames", "feature_mask", "input_sha256", "engine_revision",
      "sensitivity_db", "amplitude_gamma", "attack_ms", "release_ms",
      "spectrum_floor_db", "spectrum_ceiling_db"};
  if (fields.size() != expected.size() ||
      std::any_of(fields.begin(), fields.end(), [](const auto& field) {
        return !expected.contains(field.first);
      }) || integer_field(fields, "schema_version") != 1) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest has an unknown field or unsupported schema version.");
  }
  Manifest result;
  result.job_id = string_field(fields, "job_id");
  result.input_path = string_field(fields, "input_path");
  result.sample_count = integer_field(fields, "sample_count");
  result.max_duration_frames = integer_field(fields, "max_duration_frames");
  result.max_memory_bytes = integer_field(fields, "max_memory_bytes");
  result.max_output_bytes = integer_field(fields, "max_output_bytes");
  result.max_runtime_ms = integer_field(fields, "max_runtime_ms");
  if (result.max_runtime_ms == 0 || result.max_runtime_ms > 86400000) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Job runtime budget must be within one millisecond and 24 hours.");
  }
  const auto block_frames = integer_field(fields, "block_frames");
  const auto feature_mask = integer_field(fields, "feature_mask");
  if (block_frames > std::numeric_limits<std::uint32_t>::max() ||
      feature_mask > std::numeric_limits<unsigned>::max()) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest integer field exceeds its native representation.");
  }
  result.block_frames = static_cast<std::uint32_t>(block_frames);
  result.feature_mask = static_cast<unsigned>(feature_mask);
  result.input_sha256 = string_field(fields, "input_sha256");
  result.engine_revision = string_field(fields, "engine_revision");
  result.transform = VisualAnalysisConfig{
      number_field(fields, "sensitivity_db"),
      number_field(fields, "amplitude_gamma"),
      number_field(fields, "attack_ms"),
      number_field(fields, "release_ms"),
      number_field(fields, "spectrum_floor_db"),
      number_field(fields, "spectrum_ceiling_db")};
  return result;
}

bool safe_component(std::string_view component) {
  if (component.empty() || component == "." || component == ".." ||
      component.back() == '.' || component.back() == ' ') return false;
  if (component.size() == 2 &&
      (component[0] == 'c' || component[0] == 'C') && component[1] == ':') {
    return false;
  }
  auto stem = component.substr(0, component.find('.'));
  std::string device(stem);
  std::transform(device.begin(), device.end(), device.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (device == "CON" || device == "PRN" || device == "AUX" ||
      device == "NUL" || (device.size() == 4 &&
      (device.starts_with("COM") || device.starts_with("LPT")) &&
      device.back() >= '1' && device.back() <= '9')) return false;
  return std::all_of(component.begin(), component.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
  });
}

std::filesystem::path safe_relative(const std::filesystem::path& input) {
  const auto text = input.generic_string();
  if (input.empty() || input.is_absolute() || input.has_root_name() ||
      input.has_root_directory() || text.find('\\') != std::string::npos ||
      text.find(':') != std::string::npos) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Job manifest paths must be relative slash-separated paths.");
  }
  for (const auto& component : input) {
    if (!safe_component(component.string())) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job manifest path contains an unsafe component.");
    }
  }
  return input;
}

bool is_link_like(const std::filesystem::path& path,
                  std::error_code& error) {
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status)) return true;
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
#endif
  return false;
}

void validate_no_links(const std::filesystem::path& root,
                       const std::filesystem::path& relative,
                       bool require_regular_file) {
  std::filesystem::path current = root;
  for (auto it = relative.begin(); it != relative.end(); ++it) {
    current /= *it;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(current, error);
    if (error || is_link_like(current, error) || error) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job path contains a missing or linked component.");
    }
    const bool final = std::next(it) == relative.end();
    if (final && require_regular_file && !std::filesystem::is_regular_file(status)) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job input must be a regular file.");
    }
    if (!final && !std::filesystem::is_directory(status)) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job path parent is not a directory.");
    }
  }
}

class StagingDirectory final {
 public:
  explicit StagingDirectory(std::filesystem::path path)
      : path_(std::move(path)) {}
  ~StagingDirectory() {
    if (active_) {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    }
  }
  void published() noexcept { active_ = false; }
  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
  bool active_ = true;
};

bool publish_noreplace(const std::filesystem::path& source,
                       const std::filesystem::path& destination) {
#ifdef _WIN32
  return MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_WRITE_THROUGH) != 0;
#elif defined(__linux__) && defined(SYS_renameat2)
  return syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD,
                 destination.c_str(), RENAME_NOREPLACE) == 0;
#else
  (void)source;
  (void)destination;
  return false;
#endif
}

std::string result_json(const OfflinePackageResult& package) {
  const auto& sidecar = package.sidecar;
  return "{\"cache_key\":\"" + sidecar.cache_key +
      "\",\"chunk_count\":" + std::to_string(sidecar.chunk_count) +
      ",\"feature_count\":" + std::to_string(sidecar.feature_count) +
      ",\"metadata_sha256\":\"" + sidecar.metadata_sha256 +
      "\",\"output_bytes\":" + std::to_string(sidecar.output_bytes) +
      ",\"page_count\":" + std::to_string(sidecar.page_count) +
      ",\"page_index_sha256\":\"" + sidecar.page_index_sha256 +
      "\",\"sample_count\":" + std::to_string(sidecar.sample_count) +
      ",\"schema_version\":1,\"transform_digest\":\"" +
      sidecar.transform_digest + "\"}";
}

}  // namespace

OfflinePackageResult run(const std::filesystem::path& job_root,
                         const std::filesystem::path& manifest_relative_path,
                         std::function<bool()> cancellation_requested) {
  std::filesystem::path staging_path;
  bool time_limit_hit = false;
  try {
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(job_root, error);
    if (error || is_link_like(job_root, error) || error ||
        !std::filesystem::is_directory(root_status)) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Caller-authorized job root must be an existing non-linked directory.");
    }
    const auto root = std::filesystem::canonical(job_root, error);
    if (error) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Caller-authorized job root could not be resolved.");
    }
    const auto manifest_relative = safe_relative(manifest_relative_path);
    validate_no_links(root, manifest_relative, true);
    const auto manifest = parse_manifest(root / manifest_relative);
    if (manifest.job_id.empty() || manifest.job_id.size() > 64 ||
        !safe_component(manifest.job_id) ||
        !std::all_of(manifest.job_id.begin(), manifest.job_id.end(),
                     [](unsigned char c) {
                       return (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '_' || c == '-';
                     })) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job ID is outside its safe ASCII component contract.");
    }
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(manifest.max_runtime_ms);
    auto job_cancelled = [&] {
      if (cancellation_requested && cancellation_requested()) return true;
      if (std::chrono::steady_clock::now() >= deadline) {
        time_limit_hit = true;
        return true;
      }
      return false;
    };
    const auto input_relative = safe_relative(manifest.input_path);
    validate_no_links(root, input_relative, true);
    const auto input_path = root / input_relative;
    const auto jobs_path = root / "jobs";
    if (std::filesystem::exists(jobs_path, error)) {
      if (error || is_link_like(jobs_path, error) || error ||
          !std::filesystem::is_directory(jobs_path, error) || error) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Job output root must be a non-linked directory.");
      }
    } else {
      error.clear();
      if (!std::filesystem::create_directory(jobs_path, error) || error) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Job output root could not be created.");
      }
    }
    const auto destination = jobs_path / manifest.job_id;
    if (std::filesystem::exists(destination, error) || error) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Job destination already exists or cannot be inspected.");
    }
    static std::atomic<std::uint64_t> staging_sequence{0};
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      const auto suffix = std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) +
          "-" + std::to_string(staging_sequence.fetch_add(1));
      staging_path = jobs_path / ("." + manifest.job_id + "." + suffix +
                                  ".partial");
      error.clear();
      if (std::filesystem::create_directory(staging_path, error)) break;
      if (error) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Private job staging directory could not be reserved.");
      }
      staging_path.clear();
    }
    if (staging_path.empty()) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Private job staging directory could not be reserved.");
    }
    StagingDirectory cleanup(staging_path);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "Job input could not be opened.");
    }
    OfflineWavRequest wav_request{
        manifest.sample_count, manifest.max_duration_frames,
        manifest.max_memory_bytes, manifest.block_frames,
        manifest.input_sha256, job_cancelled};
    OfflineSidecarConfig sidecar_config{
        staging_path / "analysis-v2", manifest.sample_count,
        manifest.max_output_bytes, manifest.max_memory_bytes,
        manifest.feature_mask, manifest.transform, manifest.engine_revision,
        manifest.input_sha256};
    const auto package = analyze_wav_to_sidecar(
        input, manifest.feature_mask, manifest.transform, wav_request,
        sidecar_config);
    if (job_cancelled()) {
      if (time_limit_hit) {
        fail(OfflineAnalysisErrorCode::timed_out,
             "Offline job exceeded its explicit wall-time limit.");
      }
      fail(OfflineAnalysisErrorCode::cancelled,
           "Offline analysis was cancelled before publication.");
    }
    OfflineSidecarReader reader(sidecar_config, job_cancelled);
    if (reader.result().cache_key != package.sidecar.cache_key ||
        reader.result().output_bytes != package.sidecar.output_bytes) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Generated sidecar did not pass its read-back validation.");
    }
    const auto summary = result_json(package);
    if (summary.size() > 16 * 1024 ||
        summary.size() > manifest.max_output_bytes ||
        package.sidecar.output_bytes > manifest.max_output_bytes - summary.size()) {
      fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
           "Job result summary exceeds the reserved output budget.");
    }
    const auto partial_summary = staging_path / "result.json.partial";
    std::ofstream output(partial_summary, std::ios::binary | std::ios::out |
                                              std::ios::trunc);
    if (!output || !output.write(summary.data(),
                                 static_cast<std::streamsize>(summary.size()))) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Job result summary could not be written.");
    }
    output.close();
    if (!output) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Job result summary did not close cleanly.");
    }
    std::filesystem::rename(partial_summary, staging_path / "result.json", error);
    if (error) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Job result summary could not be finalized.");
    }
    if (job_cancelled()) {
      if (time_limit_hit) {
        fail(OfflineAnalysisErrorCode::timed_out,
             "Offline job exceeded its explicit wall-time limit.");
      }
      fail(OfflineAnalysisErrorCode::cancelled,
           "Offline analysis was cancelled before publication.");
    }
    if (!publish_noreplace(staging_path, destination)) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Validated job could not be atomically published without replacement.");
    }
    cleanup.published();
    return package;
  } catch (const OfflineAnalysisError& error) {
    if (error.code() == OfflineAnalysisErrorCode::cancelled && time_limit_hit) {
      fail(OfflineAnalysisErrorCode::timed_out,
           "Offline job exceeded its explicit wall-time limit.");
    }
    throw;
  } catch (const std::exception&) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Offline job failed before atomic publication.");
  }
}

}  // namespace shipglows::audio::offline_job
