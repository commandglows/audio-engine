#include "shipglows/audio/offline_wav_analysis.hpp"

#include "shipglows/audio/visual_analysis_wire.hpp"
#include "shipglows/audio/visual_transform_contract.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <span>
#include <system_error>

namespace shipglows::audio {
namespace {

constexpr std::uint64_t kHopFrames = 480;
constexpr std::uint64_t kRecordsPerChunk = 100;
constexpr std::uint64_t kChunksPerPage = 256;
constexpr std::uint64_t kRootMetadataReserve = 65536;
constexpr std::uint64_t kResultSummaryReserve = 16384;
constexpr std::size_t kChunkHeaderBytes = 64;
constexpr std::size_t kPageHeaderBytes = 64;
constexpr std::size_t kDescriptorBytes = 128;
constexpr std::size_t kMaximumRecordBytes = 6492;
constexpr std::size_t kMaximumChunkPayload = kRecordsPerChunk * kMaximumRecordBytes;
constexpr std::uint64_t kWriterLibraryBufferAllowance = 65536;

[[noreturn]] void fail(OfflineAnalysisErrorCode code, const char* message) {
  throw OfflineAnalysisError(code, message);
}

bool lower_hex(const std::string& value, std::size_t expected) {
  return value.size() == expected &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

std::uint64_t checked_add(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Sidecar output-size arithmetic overflowed.");
  }
  return left + right;
}

std::uint64_t checked_multiply(std::uint64_t left, std::uint64_t right) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Sidecar output-size arithmetic overflowed.");
  }
  return left * right;
}

std::uint64_t ceil_div(std::uint64_t value, std::uint64_t divisor) {
  return value / divisor + (value % divisor == 0 ? 0 : 1);
}

void put_u32(std::uint8_t* destination, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    destination[shift / 8] = static_cast<std::uint8_t>(value >> shift);
  }
}

void put_u64(std::uint8_t* destination, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    destination[shift / 8] = static_cast<std::uint8_t>(value >> shift);
  }
}

std::array<std::uint8_t, 64> make_page_header(const char* magic,
                                              std::uint64_t first_index,
                                              std::uint32_t count) {
  std::array<std::uint8_t, 64> header{};
  std::memcpy(header.data(), magic, 8);
  put_u32(header.data() + 8, 2);
  put_u32(header.data() + 12, 128);
  put_u64(header.data() + 16, first_index);
  put_u32(header.data() + 24, count);
  put_u32(header.data() + 28, 0);
  put_u64(header.data() + 32, static_cast<std::uint64_t>(count) * 128);
  return header;
}

std::array<std::uint8_t, 64> make_chunk_header(
    std::uint32_t record_bytes, std::uint64_t first_record,
    std::uint32_t count, unsigned feature_mask, std::uint32_t profile_code,
    std::uint64_t first_center, std::uint64_t payload_bytes) {
  std::array<std::uint8_t, 64> header{};
  std::memcpy(header.data(), "SGAVC002", 8);
  put_u32(header.data() + 8, 2);
  put_u32(header.data() + 12, record_bytes);
  put_u64(header.data() + 16, first_record);
  put_u32(header.data() + 24, count);
  put_u32(header.data() + 28, feature_mask);
  put_u32(header.data() + 32, profile_code);
  put_u32(header.data() + 36, 0);
  put_u64(header.data() + 40, first_center);
  put_u64(header.data() + 48, payload_bytes);
  put_u32(header.data() + 56, 64);
  put_u32(header.data() + 60, 0);
  return header;
}

std::array<std::uint8_t, 128> make_descriptor(
    std::uint64_t index, std::uint64_t first_record, std::uint32_t count,
    std::uint64_t file_bytes, const std::string& digest,
    const std::string& path) {
  std::array<std::uint8_t, 128> descriptor{};
  put_u64(descriptor.data(), index);
  put_u64(descriptor.data() + 8, first_record);
  put_u32(descriptor.data() + 16, count);
  put_u32(descriptor.data() + 20, 0);
  put_u64(descriptor.data() + 24, file_bytes);
  if (!lower_hex(digest, 64) || path.empty() || path.size() >= 64) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar descriptor identity is invalid.");
  }
  for (std::size_t i = 0; i < 32; ++i) {
    const auto byte = static_cast<unsigned>(std::stoul(digest.substr(i * 2, 2),
                                                       nullptr, 16));
    descriptor[32 + i] = static_cast<std::uint8_t>(byte);
  }
  std::memcpy(descriptor.data() + 64, path.data(), path.size());
  return descriptor;
}

std::string ordinal_path(const char* prefix, std::uint64_t ordinal,
                         const char* extension) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::array<char, 16> hex{};
  for (std::size_t index = 0; index < hex.size(); ++index) {
    const auto shift = static_cast<unsigned>((hex.size() - index - 1) * 4);
    hex[index] = kDigits[(ordinal >> shift) & 0xfU];
  }
  return std::string(prefix) + std::string(hex.data(), hex.size()) + extension;
}

std::uint32_t profile_code(unsigned feature_mask) {
  if (feature_mask == 3 || feature_mask == 11) return 1;
  if (feature_mask == 5 || feature_mask == 13) return 2;
  fail(OfflineAnalysisErrorCode::output_validation_failed,
       "Sidecar feature mask is unsupported.");
}

const char* profile_name(unsigned feature_mask) {
  return profile_code(feature_mask) == 1 ? "sgav-music-1" : "sgav-speech-1";
}

void hash_bytes(Sha256& hash, const void* data, std::size_t size) {
  hash.update(std::as_bytes(std::span(static_cast<const char*>(data), size)));
}

void write_bytes(std::ostream& output, const void* data, std::size_t size) {
  if (size != 0) output.write(static_cast<const char*>(data),
                              static_cast<std::streamsize>(size));
  if (!output) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar file write failed.");
  }
}

std::string cache_key_json(const OfflineSidecarConfig& config,
                           const std::string& transform_json,
                           const std::string& transform_digest) {
  return "{\"channels\":2,\"engine_revision\":\"" +
         config.engine_revision + "\",\"feature_mask\":" +
         std::to_string(config.feature_mask) +
         ",\"hop_frames\":480,\"input_sha256\":\"" +
         config.input_sha256 + "\",\"profile\":\"" +
         profile_name(config.feature_mask) + "\",\"record_layout\":\""
         "sgav-record-2-compact-speech-1\",\"sample_count\":" +
         std::to_string(config.sample_count) +
         ",\"sample_rate\":48000,\"schema_version\":2,"
         "\"transform_config\":" + transform_json +
         ",\"transform_digest\":\"" + transform_digest +
         "\",\"transform_version\":\"sgav-visual-transform-1\"}";
}

std::string calculate_cache_key(const OfflineSidecarConfig& config,
                                const std::string& transform_json,
                                const std::string& transform_digest) {
  const auto effective = cache_key_json(config, transform_json,
                                        transform_digest);
  const std::string material = "sgav-cache-2\n" + effective;
  Sha256 hash;
  hash_bytes(hash, material.data(), material.size());
  return Sha256::hex(hash.finalize());
}

std::uint64_t expected_working_bytes(unsigned feature_mask) {
  const auto record_bytes = visual_analysis_record_bytes(feature_mask);
  return sizeof(OfflineSidecarWriter) + kMaximumChunkPayload + record_bytes +
         kWriterLibraryBufferAllowance;
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

std::vector<std::uint8_t> read_bounded_file(const std::filesystem::path& path,
                                           std::uint64_t maximum) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || std::filesystem::is_symlink(status) ||
      !std::filesystem::is_regular_file(status)) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar entry is missing, linked, or not a regular file.");
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximum ||
      size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar file length is outside its bounded contract.");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  if (!input || (!bytes.empty() &&
      !input.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size())))) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar file could not be read completely.");
  }
  return bytes;
}

std::string json_raw_value(const std::string& json, const char* key) {
  const std::string marker = std::string("\"") + key + "\":";
  const auto first = json.find(marker);
  if (first == std::string::npos ||
      json.find(marker, first + marker.size()) != std::string::npos) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar metadata has a missing or duplicate required field.");
  }
  const auto begin = first + marker.size();
  if (begin >= json.size()) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar metadata field has no value.");
  }
  std::size_t end = begin;
  if (json[begin] == '"') {
    ++end;
    bool escaped = false;
    for (; end < json.size(); ++end) {
      if (escaped) escaped = false;
      else if (json[end] == '\\') escaped = true;
      else if (json[end] == '"') { ++end; break; }
    }
    if (end > json.size() || json[end - 1] != '"') {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar metadata string is unterminated.");
    }
  } else if (json[begin] == '{' || json[begin] == '[') {
    std::vector<char> closes;
    bool in_string = false;
    bool escaped = false;
    for (; end < json.size(); ++end) {
      const char value = json[end];
      if (in_string) {
        if (escaped) escaped = false;
        else if (value == '\\') escaped = true;
        else if (value == '"') in_string = false;
        continue;
      }
      if (value == '"') in_string = true;
      else if (value == '{') closes.push_back('}');
      else if (value == '[') closes.push_back(']');
      else if (value == '}' || value == ']') {
        if (closes.empty() || closes.back() != value) {
          fail(OfflineAnalysisErrorCode::output_validation_failed,
               "Sidecar metadata contains malformed nested JSON.");
        }
        closes.pop_back();
        if (closes.empty()) { ++end; break; }
      }
    }
    if (!closes.empty() || in_string) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar metadata nested value is incomplete.");
    }
  } else {
    while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
  }
  if (end <= begin) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar metadata value is empty.");
  }
  return json.substr(begin, end - begin);
}

std::uint64_t metadata_u64(const std::string& json, const char* key) {
  const auto raw = json_raw_value(json, key);
  std::uint64_t value = 0;
  if (raw.empty() || (raw.size() > 1 && raw.front() == '0')) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar metadata integer is not canonical.");
  }
  for (const char digit : raw) {
    if (digit < '0' || digit > '9' ||
        value > (std::numeric_limits<std::uint64_t>::max() -
                 static_cast<unsigned>(digit - '0')) / 10) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar metadata integer is invalid or overflows.");
    }
    value = value * 10 + static_cast<unsigned>(digit - '0');
  }
  return value;
}

std::string metadata_string(const std::string& json, const char* key) {
  auto raw = json_raw_value(json, key);
  if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"' ||
      raw.find('\\') != std::string::npos) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar metadata string is not in canonical form.");
  }
  return raw.substr(1, raw.size() - 2);
}

std::string descriptor_path(const std::uint8_t* descriptor) {
  constexpr std::size_t kPathOffset = 64;
  constexpr std::size_t kPathBytes = 64;
  std::size_t length = 0;
  while (length < kPathBytes && descriptor[kPathOffset + length] != 0) ++length;
  if (length == 0 || length == kPathBytes ||
      std::any_of(descriptor + kPathOffset + length,
                  descriptor + kPathOffset + kPathBytes,
                  [](std::uint8_t value) { return value != 0; })) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar descriptor path is not a bounded zero-padded name.");
  }
  return std::string(reinterpret_cast<const char*>(descriptor + kPathOffset),
                     length);
}

void validate_descriptor(const std::uint8_t* descriptor, std::uint64_t index,
                         std::uint64_t first_record, std::uint32_t count,
                         const std::string& path, std::uint64_t minimum_bytes) {
  if (get_u64(descriptor) != index || get_u64(descriptor + 8) != first_record ||
      get_u32(descriptor + 16) != count || get_u32(descriptor + 20) != 0 ||
      get_u64(descriptor + 24) < minimum_bytes ||
      descriptor_path(descriptor) != path) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar descriptor identity or range is invalid.");
  }
}

void validate_encoded_records(const std::uint8_t* payload, std::uint32_t count,
                              std::uint32_t record_bytes, std::uint64_t first,
                              std::uint64_t sample_count, unsigned mask) {
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto* record = payload + static_cast<std::size_t>(i) * record_bytes;
    const auto index = first + i;
    const auto center = checked_multiply(index, kHopFrames);
    if (center >= sample_count) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar record begins beyond the source length.");
    }
    const auto valid = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kHopFrames, sample_count - center));
    if (get_u64(record) != index + 1 ||
        get_u64(record + 8) != center || get_u64(record + 16) != 1 ||
        get_u32(record + 24) != valid ||
        get_u32(record + 28) != ((mask & 2U) ? 64U : 0U) ||
        get_u32(record + 32) != 0 || get_u32(record + 36) != 0) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar record header does not match its source position.");
    }
    const auto read_float = [](const std::uint8_t* bytes) {
      const auto bits = get_u32(bytes);
      float value = 0;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    };
    const auto bounded = [&](std::size_t offset, std::size_t count_values,
                             float minimum, float maximum) {
      for (std::size_t j = 0; j < count_values; ++j) {
        const auto value = read_float(record + offset + j * 4);
        if (!std::isfinite(value) || value < minimum || value > maximum) {
          return false;
        }
      }
      return true;
    };
    if (!bounded(40, 6, 0, 8) || !bounded(64, 6, 0, 1)) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar record levels are outside finite bounds.");
    }
    constexpr float tolerance = 1e-6F;
    const float left_peak = read_float(record + 40);
    const float right_peak = read_float(record + 44);
    const float combined_peak = read_float(record + 48);
    const float left_rms = read_float(record + 52);
    const float right_rms = read_float(record + 56);
    const float combined_rms = read_float(record + 60);
    if (std::abs(combined_peak - std::max(left_peak, right_peak)) > tolerance ||
        std::abs(combined_rms - std::sqrt(
            (left_rms * left_rms + right_rms * right_rms) * .5F)) > tolerance) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar record combined levels are inconsistent.");
    }
    std::size_t offset = 88;
    if ((mask & 2U) != 0) {
      if (!bounded(offset, 256, -8, 8) || !bounded(offset + 1024, 256, -1, 1)) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Sidecar record envelope is outside finite bounds.");
      }
      for (std::size_t j = 0; j < 256; j += 2) {
        if (read_float(record + offset + j * 4) >
            read_float(record + offset + (j + 1) * 4)) {
          fail(OfflineAnalysisErrorCode::output_validation_failed,
               "Sidecar envelope minimum exceeds its maximum.");
        }
      }
      offset += 2048;
    }
    if ((mask & 8U) != 0 &&
        (!bounded(offset, 1025, 0, 64) ||
         !bounded(offset + 4100, 64, 0, 1))) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar record spectrum is outside finite bounds.");
    }
  }
}

bool finite_between(float value, float minimum, float maximum) {
  return std::isfinite(value) && value >= minimum && value <= maximum;
}

void validate_record(const VisualAnalysisRecord& record, std::uint64_t index,
                     std::uint64_t sample_count, unsigned feature_mask) {
  const auto center = checked_multiply(index, kHopFrames);
  if (center >= sample_count || record.origin != 0 ||
      record.sequence != index + 1 ||
      record.center != center || record.continuity != 1 || record.flags != 0 ||
      record.valid_frames != std::min<std::uint64_t>(kHopFrames,
                                                      sample_count - center)) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Offline record position, continuity, flags, or final-hop length is invalid.");
  }
  for (const auto value : record.levels) {
    if (!finite_between(value, 0, 8)) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Physical level is outside the finite contract.");
    }
  }
  for (const auto value : record.visual_levels) {
    if (!finite_between(value, 0, 1)) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Visual level is outside the finite contract.");
    }
  }
  constexpr float kTolerance = 1e-6F;
  if (std::abs(record.levels[2] -
               std::max(record.levels[0], record.levels[1])) > kTolerance ||
      std::abs(record.levels[5] - std::sqrt(
          (record.levels[3] * record.levels[3] +
           record.levels[4] * record.levels[4]) * .5F)) > kTolerance) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Combined peak or RMS does not match its channel levels.");
  }
  if ((feature_mask & 2) != 0) {
    for (std::size_t envelope = 0; envelope < record.envelope.size(); ++envelope) {
      if (!finite_between(record.envelope[envelope], -8, 8) ||
          !finite_between(record.visual_envelope[envelope], -1, 1)) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Music envelope is outside the finite contract.");
      }
      if ((envelope % 2) == 0 &&
          record.envelope[envelope] > record.envelope[envelope + 1]) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Music envelope minimum exceeds its maximum.");
      }
    }
  }
  if ((feature_mask & 8) != 0) {
    for (const auto value : record.spectrum) {
      if (!finite_between(value, 0, 64)) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Physical spectrum is outside the finite contract.");
      }
    }
    for (const auto value : record.bands) {
      if (!finite_between(value, 0, 1)) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Visual spectrum band is outside the finite contract.");
      }
    }
  }
}

std::string root_metadata_json(const OfflineSidecarConfig& config,
                               const OfflineSidecarResult& result,
                               const std::string& transform_json) {
  return "{\"cache_key\":\"" + result.cache_key +
         "\",\"channels\":2,\"chunk_count\":" +
         std::to_string(result.chunk_count) +
         ",\"chunk_records\":100,\"chunks_per_page\":256,"
         "\"engine_revision\":\"" + config.engine_revision +
         "\",\"feature_count\":" + std::to_string(result.feature_count) +
         ",\"feature_mask\":" + std::to_string(config.feature_mask) +
         ",\"hop_frames\":480,\"input_sha256\":\"" +
         config.input_sha256 + "\",\"page_count\":" +
         std::to_string(result.page_count) +
         ",\"page_index_bytes\":" + std::to_string(64 + result.page_count * 128) +
         ",\"page_index_path\":\"pages.sgai\",\"page_index_sha256\":\"" +
         result.page_index_sha256 + "\",\"profile\":\"" +
         profile_name(config.feature_mask) + "\",\"profile_code\":" +
         std::to_string(profile_code(config.feature_mask)) +
         ",\"record_bytes\":" + std::to_string(visual_analysis_record_bytes(
             config.feature_mask)) +
         ",\"record_layout\":\"sgav-record-2-compact-speech-1\","
         "\"sample_count\":" + std::to_string(config.sample_count) +
         ",\"sample_rate\":48000,\"schema_version\":2,\"source_identity\":\"" +
         config.input_sha256 + "\",\"transform_config\":" + transform_json +
         ",\"transform_digest\":\"" + result.transform_digest +
         "\",\"transform_version\":\"sgav-visual-transform-1\"}";
}

}  // namespace

std::uint64_t offline_sidecar_output_reserve(std::uint64_t sample_count,
                                             unsigned feature_mask) {
  const auto record_bytes = visual_analysis_record_bytes(feature_mask);
  const auto features = ceil_div(sample_count, kHopFrames);
  const auto chunks = ceil_div(features, kRecordsPerChunk);
  const auto pages = ceil_div(chunks, kChunksPerPage);
  auto reserve = checked_multiply(features, record_bytes);
  reserve = checked_add(reserve, checked_multiply(chunks, 192));
  reserve = checked_add(reserve, checked_multiply(pages, 192));
  reserve = checked_add(reserve, 64 + kRootMetadataReserve +
                                     kResultSummaryReserve);
  return reserve;
}

std::uint64_t offline_sidecar_working_bytes(unsigned feature_mask) {
  return expected_working_bytes(feature_mask);
}

OfflineSidecarWriter::OfflineSidecarWriter(OfflineSidecarConfig config)
    : config_(std::move(config)) {
  try {
    record_bytes_ = static_cast<std::uint32_t>(
        visual_analysis_record_bytes(config_.feature_mask));
    (void)profile_code(config_.feature_mask);
    if (config_.max_output_bytes == 0 || config_.max_memory_bytes == 0 ||
        !lower_hex(config_.input_sha256, 64) ||
        !(config_.engine_revision.size() == 40 ||
          config_.engine_revision.size() == 64) ||
        !lower_hex(config_.engine_revision,
                   config_.engine_revision.size())) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar identity or explicit output limits are invalid.");
    }
    transform_json_ = visual_transform_config_json(config_.transform);
    transform_digest_ = visual_transform_config_digest(config_.transform);
    feature_count_ = ceil_div(config_.sample_count, kHopFrames);
    chunk_count_ = ceil_div(feature_count_, kRecordsPerChunk);
    page_count_ = ceil_div(chunk_count_, kChunksPerPage);
    if (page_count_ > std::numeric_limits<std::uint32_t>::max()) {
      fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
           "Sidecar page count exceeds the v2 index header limit.");
    }
    reserve_bytes_ = offline_sidecar_output_reserve(config_.sample_count,
                                                     config_.feature_mask);
    if (reserve_bytes_ > config_.max_output_bytes) {
      fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
           "Exact sidecar output reserve exceeds the explicit output budget.");
    }
    const auto working = expected_working_bytes(config_.feature_mask);
    if (working > config_.max_memory_bytes) {
      fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
           "Sidecar writer buffers exceed the explicit memory budget.");
    }
    cache_key_ = calculate_cache_key(config_, transform_json_,
                                     transform_digest_);
    if (config_.analysis_directory.empty()) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar staging directory is missing.");
    }
    std::error_code error;
    if (std::filesystem::exists(config_.analysis_directory, error) || error) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar staging directory must be new and private.");
    }
    std::filesystem::create_directories(config_.analysis_directory / "chunks", error);
    if (error) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar chunk directory could not be created.");
    }
    std::filesystem::create_directories(config_.analysis_directory / "pages", error);
    if (error) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar page directory could not be created.");
    }
    const auto max_payload = static_cast<std::size_t>(kRecordsPerChunk) *
                             record_bytes_;
    chunk_payload_.reset(new (std::nothrow) std::uint8_t[max_payload]);
    if (!chunk_payload_) {
      fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
           "Sidecar chunk buffer could not be admitted in memory.");
    }
    page_index_stream_.open(config_.analysis_directory / "pages.sgai",
                            std::ios::binary | std::ios::out | std::ios::trunc);
    if (!page_index_stream_) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar page index could not be created.");
    }
    const auto index_header = make_page_header(
        "SGAVI002", 0, static_cast<std::uint32_t>(page_count_));
    write_bytes(page_index_stream_, index_header.data(), index_header.size());
    hash_bytes(page_index_hash_, index_header.data(), index_header.size());
    add_output_bytes(index_header.size());
  } catch (const OfflineAnalysisError&) {
    throw;
  } catch (const std::exception&) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar writer admission failed.");
  }
}

OfflineSidecarWriter::~OfflineSidecarWriter() {
  if (page_index_stream_.is_open()) page_index_stream_.close();
}

void OfflineSidecarWriter::add_output_bytes(std::uint64_t bytes) {
  output_bytes_ = checked_add(output_bytes_, bytes);
  if (output_bytes_ > config_.max_output_bytes) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Actual sidecar bytes exceed the explicit output budget.");
  }
}

std::uint64_t OfflineSidecarWriter::working_bytes() const noexcept {
  return expected_working_bytes(config_.feature_mask);
}

void OfflineSidecarWriter::append(const VisualAnalysisRecord& record) {
  if (finished_ || records_written_ >= feature_count_) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar received more records than its admitted source length.");
  }
  validate_record(record, records_written_, config_.sample_count,
                  config_.feature_mask);
  if (records_in_chunk_ == 0) first_center_frame_ = record.center;
  auto encoded = encode_visual_analysis_record(record, config_.feature_mask);
  if (encoded.size() != record_bytes_ ||
      chunk_payload_bytes_ + encoded.size() >
          static_cast<std::size_t>(kRecordsPerChunk) * record_bytes_) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar record length exceeds the fixed chunk capacity.");
  }
  std::memcpy(chunk_payload_.get() + chunk_payload_bytes_, encoded.data(),
              encoded.size());
  chunk_payload_bytes_ += encoded.size();
  ++records_in_chunk_;
  ++records_written_;
  if (records_in_chunk_ == kRecordsPerChunk) flush_chunk();
}

void OfflineSidecarWriter::flush_chunk() {
  if (records_in_chunk_ == 0) return;
  const auto chunk_index = chunks_written_;
  const auto first_record = checked_multiply(chunk_index, kRecordsPerChunk);
  const auto payload_bytes = static_cast<std::uint64_t>(chunk_payload_bytes_);
  const auto file_bytes = kChunkHeaderBytes + payload_bytes;
  const auto header = make_chunk_header(
      record_bytes_, first_record, records_in_chunk_, config_.feature_mask,
      profile_code(config_.feature_mask), first_center_frame_, payload_bytes);
  const auto relative_path = ordinal_path("chunks/chunk-", chunk_index, ".sgav");
  const auto full_path = config_.analysis_directory /
                         std::filesystem::path(relative_path);
  std::ofstream output(full_path, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!output) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar chunk file could not be created.");
  }
  Sha256 digest;
  write_bytes(output, header.data(), header.size());
  write_bytes(output, chunk_payload_.get(), chunk_payload_bytes_);
  hash_bytes(digest, header.data(), header.size());
  hash_bytes(digest, chunk_payload_.get(), chunk_payload_bytes_);
  output.close();
  if (!output) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar chunk file did not close cleanly.");
  }
  add_output_bytes(file_bytes);
  const auto descriptor = make_descriptor(
      chunk_index, first_record, records_in_chunk_, file_bytes,
      Sha256::hex(digest.finalize()), relative_path);
  if (descriptors_in_page_ == 0) page_first_record_ = first_record;
  page_descriptors_[descriptors_in_page_++] = descriptor;
  ++chunks_written_;
  records_in_chunk_ = 0;
  chunk_payload_bytes_ = 0;
  if (descriptors_in_page_ == kChunksPerPage) flush_page();
}

void OfflineSidecarWriter::flush_page() {
  if (descriptors_in_page_ == 0) return;
  const auto page_index = pages_written_;
  const auto first_chunk = checked_multiply(page_index, kChunksPerPage);
  const auto page_file_bytes = kPageHeaderBytes +
      static_cast<std::uint64_t>(descriptors_in_page_) * kDescriptorBytes;
  const auto header = make_page_header(
      "SGAVP002", first_chunk, descriptors_in_page_);
  const auto relative_path = ordinal_path("pages/page-", page_index, ".sgip");
  const auto full_path = config_.analysis_directory /
                         std::filesystem::path(relative_path);
  std::ofstream output(full_path, std::ios::binary | std::ios::out | std::ios::trunc);
  if (!output) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar page file could not be created.");
  }
  Sha256 digest;
  write_bytes(output, header.data(), header.size());
  hash_bytes(digest, header.data(), header.size());
  for (std::uint32_t index = 0; index < descriptors_in_page_; ++index) {
    const auto& descriptor = page_descriptors_[index];
    write_bytes(output, descriptor.data(), descriptor.size());
    hash_bytes(digest, descriptor.data(), descriptor.size());
  }
  output.close();
  if (!output) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar page file did not close cleanly.");
  }
  add_output_bytes(page_file_bytes);

  const auto page_descriptor = make_descriptor(
      first_chunk, page_first_record_, descriptors_in_page_, page_file_bytes,
      Sha256::hex(digest.finalize()), relative_path);
  write_bytes(page_index_stream_, page_descriptor.data(), page_descriptor.size());
  hash_bytes(page_index_hash_, page_descriptor.data(), page_descriptor.size());
  add_output_bytes(page_descriptor.size());
  ++pages_written_;
  descriptors_in_page_ = 0;
  page_first_record_ = 0;
}

OfflineSidecarResult OfflineSidecarWriter::finish() {
  if (finished_) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar writer was already finalized.");
  }
  if (records_written_ != feature_count_) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar is missing one or more source feature records.");
  }
  flush_chunk();
  flush_page();
  if (chunks_written_ != chunk_count_ || pages_written_ != page_count_) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar chunk or page coverage does not match its admitted source length.");
  }
  page_index_stream_.flush();
  page_index_stream_.close();
  if (!page_index_stream_) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar page index did not close cleanly.");
  }
  const auto index_bytes = checked_add(64, checked_multiply(page_count_, 128));
  std::error_code index_error;
  const auto actual_index_bytes = std::filesystem::file_size(
      config_.analysis_directory / "pages.sgai", index_error);
  if (index_error || actual_index_bytes != index_bytes) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar page index length differs from its admitted descriptor count.");
  }

  OfflineSidecarResult result;
  result.sample_count = config_.sample_count;
  result.feature_count = feature_count_;
  result.chunk_count = chunk_count_;
  result.page_count = page_count_;
  result.reserved_output_bytes = reserve_bytes_;
  result.working_bytes = working_bytes();
  result.transform_digest = transform_digest_;
  result.cache_key = cache_key_;
  result.page_index_sha256 = Sha256::hex(page_index_hash_.finalize());

  const auto metadata = root_metadata_json(config_, result, transform_json_);
  if (metadata.size() > kRootMetadataReserve) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Sidecar root metadata exceeds the 64 KiB contract.");
  }
  const auto temporary_path = config_.analysis_directory / "metadata.json.partial";
  const auto metadata_path = config_.analysis_directory / "metadata.json";
  std::ofstream output(temporary_path,
                       std::ios::binary | std::ios::out | std::ios::trunc);
  if (!output) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar root metadata could not be staged.");
  }
  write_bytes(output, metadata.data(), metadata.size());
  output.close();
  if (!output) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar root metadata did not close cleanly.");
  }
  std::error_code error;
  std::filesystem::rename(temporary_path, metadata_path, error);
  if (error) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar root metadata could not be atomically finalized.");
  }
  Sha256 metadata_hash;
  hash_bytes(metadata_hash, metadata.data(), metadata.size());
  result.metadata_sha256 = Sha256::hex(metadata_hash.finalize());
  result.output_bytes = checked_add(output_bytes_, metadata.size());
  if (result.output_bytes > config_.max_output_bytes) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Final sidecar output exceeds the explicit output budget.");
  }
  finished_ = true;
  return result;
}

namespace {

std::uint64_t reader_working_bytes(unsigned feature_mask) {
  return 1024ULL * 1024 +
         100ULL * visual_analysis_record_bytes(feature_mask) + 64;
}

bool is_expected_ordinal_path(const std::string& relative,
                              const char* prefix, const char* extension,
                              std::uint64_t limit) {
  const std::string start(prefix);
  const std::string suffix(extension);
  if (relative.size() != start.size() + 16 + suffix.size() ||
      relative.compare(0, start.size(), start) != 0 ||
      relative.compare(relative.size() - suffix.size(), suffix.size(), suffix) != 0) {
    return false;
  }
  std::uint64_t ordinal = 0;
  for (std::size_t i = start.size(); i < start.size() + 16; ++i) {
    const char digit = relative[i];
    unsigned value = 0;
    if (digit >= '0' && digit <= '9') value = static_cast<unsigned>(digit - '0');
    else if (digit >= 'a' && digit <= 'f') value = 10U + digit - 'a';
    else return false;
    ordinal = (ordinal << 4U) | value;
  }
  return ordinal < limit;
}

std::vector<std::uint8_t> read_chunk_payload(
    const std::filesystem::path& directory, const std::uint8_t* descriptor,
    std::uint64_t chunk_index, std::uint64_t first_record,
    std::uint32_t count, std::uint32_t record_bytes, unsigned mask,
    std::uint64_t sample_count) {
  const auto path = ordinal_path("chunks/chunk-", chunk_index, ".sgav");
  const auto expected_file_bytes = 64ULL +
      static_cast<std::uint64_t>(count) * record_bytes;
  validate_descriptor(descriptor, chunk_index, first_record, count, path,
                      expected_file_bytes);
  if (get_u64(descriptor + 24) != expected_file_bytes) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar chunk descriptor length is not exact.");
  }
  const auto bytes = read_bounded_file(directory / path, expected_file_bytes);
  if (bytes.size() != expected_file_bytes ||
      std::memcmp(bytes.data(), "SGAVC002", 8) != 0 ||
      get_u32(bytes.data() + 8) != 2 ||
      get_u32(bytes.data() + 12) != record_bytes ||
      get_u64(bytes.data() + 16) != first_record ||
      get_u32(bytes.data() + 24) != count ||
      get_u32(bytes.data() + 28) != mask ||
      get_u32(bytes.data() + 32) != profile_code(mask) ||
      get_u32(bytes.data() + 36) != 0 ||
      get_u64(bytes.data() + 40) != checked_multiply(first_record, kHopFrames) ||
      get_u64(bytes.data() + 48) !=
          static_cast<std::uint64_t>(count) * record_bytes ||
      get_u32(bytes.data() + 56) != 64 || get_u32(bytes.data() + 60) != 0) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar chunk header or exact length is invalid.");
  }
  Sha256 digest;
  hash_bytes(digest, bytes.data(), bytes.size());
  const auto expected_digest = [&] {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (std::size_t i = 32; i < 64; ++i) {
      result.push_back(digits[descriptor[i] >> 4]);
      result.push_back(digits[descriptor[i] & 0xf]);
    }
    return result;
  }();
  if (Sha256::hex(digest.finalize()) != expected_digest) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar chunk digest does not match its page descriptor.");
  }
  validate_encoded_records(bytes.data() + 64, count, record_bytes,
                           first_record, sample_count, mask);
  return bytes;
}

std::string digest_from_descriptor(const std::uint8_t* descriptor) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (std::size_t i = 32; i < 64; ++i) {
    result.push_back(digits[descriptor[i] >> 4]);
    result.push_back(digits[descriptor[i] & 0xf]);
  }
  return result;
}

void validate_page_file(const std::filesystem::path& directory,
                        const std::uint8_t* page_descriptor,
                        std::uint64_t page_index,
                        std::uint64_t first_chunk,
                        std::uint32_t chunk_count,
                        std::uint64_t first_record) {
  const auto relative = ordinal_path("pages/page-", page_index, ".sgip");
  const auto bytes_count = 64ULL +
      static_cast<std::uint64_t>(chunk_count) * kDescriptorBytes;
  validate_descriptor(page_descriptor, first_chunk, first_record, chunk_count,
                      relative, bytes_count);
  if (get_u64(page_descriptor + 24) != bytes_count) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar page descriptor length is not exact.");
  }
  const auto bytes = read_bounded_file(directory / relative, bytes_count);
  if (bytes.size() != bytes_count ||
      std::memcmp(bytes.data(), "SGAVP002", 8) != 0 ||
      get_u32(bytes.data() + 8) != 2 || get_u32(bytes.data() + 12) != 128 ||
      get_u64(bytes.data() + 16) != first_chunk ||
      get_u32(bytes.data() + 24) != chunk_count ||
      get_u32(bytes.data() + 28) != 0 ||
      get_u64(bytes.data() + 32) !=
          static_cast<std::uint64_t>(chunk_count) * 128) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar page header or exact length is invalid.");
  }
  Sha256 hash;
  hash_bytes(hash, bytes.data(), bytes.size());
  if (Sha256::hex(hash.finalize()) != digest_from_descriptor(page_descriptor)) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar page digest does not match its index descriptor.");
  }
}

}  // namespace

OfflineSidecarReader::OfflineSidecarReader(
    OfflineSidecarConfig expected,
    std::function<bool()> cancellation_requested)
    : expected_(std::move(expected)),
      cancellation_requested_(std::move(cancellation_requested)) {
  try {
    if (cancellation_requested_ && cancellation_requested_()) {
      fail(OfflineAnalysisErrorCode::cancelled,
           "Sidecar validation was cancelled by its caller.");
    }
    (void)profile_code(expected_.feature_mask);
    const auto record_bytes = static_cast<std::uint32_t>(
        visual_analysis_record_bytes(expected_.feature_mask));
    const auto transform_json = visual_transform_config_json(expected_.transform);
    const auto transform_digest =
        visual_transform_config_digest(expected_.transform);
    const auto expected_working = reader_working_bytes(expected_.feature_mask);
    if (expected_.analysis_directory.empty() || expected_.max_memory_bytes == 0 ||
        expected_working > expected_.max_memory_bytes ||
        !lower_hex(expected_.input_sha256, 64) ||
        !(expected_.engine_revision.size() == 40 ||
          expected_.engine_revision.size() == 64) ||
        !lower_hex(expected_.engine_revision, expected_.engine_revision.size())) {
      fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
           "Sidecar reader identity or bounded memory admission is invalid.");
    }
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(
        expected_.analysis_directory, error);
    if (error || std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status)) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar directory is missing, linked, or not a directory.");
    }
    const auto metadata = read_bounded_file(
        expected_.analysis_directory / "metadata.json", kRootMetadataReserve);
    const std::string metadata_json(metadata.begin(), metadata.end());
    if (metadata.empty() || metadata_json.front() != '{' ||
        metadata_json.back() != '}') {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar root metadata is not a complete JSON object.");
    }
    result_.sample_count = expected_.sample_count;
    result_.feature_count = ceil_div(expected_.sample_count, kHopFrames);
    result_.chunk_count = ceil_div(result_.feature_count, kRecordsPerChunk);
    result_.page_count = ceil_div(result_.chunk_count, kChunksPerPage);
    result_.reserved_output_bytes = offline_sidecar_output_reserve(
        expected_.sample_count, expected_.feature_mask);
    result_.working_bytes = expected_working;
    result_.transform_digest = transform_digest;
    result_.cache_key = calculate_cache_key(expected_, transform_json,
                                            transform_digest);
    if (metadata_u64(metadata_json, "feature_count") != result_.feature_count ||
        metadata_u64(metadata_json, "chunk_count") != result_.chunk_count ||
        metadata_u64(metadata_json, "page_count") != result_.page_count ||
        metadata_u64(metadata_json, "page_index_bytes") !=
            64 + result_.page_count * 128) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar root counts do not match the expected source length.");
    }
    result_.page_index_sha256 = metadata_string(metadata_json,
                                                "page_index_sha256");
    if (!lower_hex(result_.page_index_sha256, 64)) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar page-index digest is malformed.");
    }
    const auto canonical_metadata = root_metadata_json(expected_, result_,
                                                       transform_json);
    if (metadata_json != canonical_metadata) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar root metadata differs from the canonical expected identity.");
    }
    Sha256 metadata_hash;
    hash_bytes(metadata_hash, metadata.data(), metadata.size());
    result_.metadata_sha256 = Sha256::hex(metadata_hash.finalize());

    const auto index_path = expected_.analysis_directory / "pages.sgai";
    const auto index_bytes = 64 + result_.page_count * 128;
    const auto index_status = std::filesystem::symlink_status(index_path, error);
    if (error || std::filesystem::is_symlink(index_status) ||
        !std::filesystem::is_regular_file(index_status) ||
        std::filesystem::file_size(index_path, error) != index_bytes || error ||
        sha256_file(index_path) != result_.page_index_sha256) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar page index length or digest is invalid.");
    }
    std::ifstream index(index_path, std::ios::binary);
    std::array<std::uint8_t, 64> index_header{};
    if (!index || !index.read(reinterpret_cast<char*>(index_header.data()),
                              index_header.size()) ||
        std::memcmp(index_header.data(), "SGAVI002", 8) != 0 ||
        get_u32(index_header.data() + 8) != 2 ||
        get_u32(index_header.data() + 12) != 128 ||
        get_u64(index_header.data() + 16) != 0 ||
        get_u32(index_header.data() + 24) != result_.page_count ||
        get_u32(index_header.data() + 28) != 0 ||
        get_u64(index_header.data() + 32) != result_.page_count * 128) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar page-index header is invalid.");
    }
    std::uint64_t output_bytes = metadata.size() + index_bytes;
    std::array<std::uint8_t, 128> page_descriptor{};
    for (std::uint64_t page = 0; page < result_.page_count; ++page) {
      if (cancellation_requested_ && cancellation_requested_()) {
        fail(OfflineAnalysisErrorCode::cancelled,
             "Sidecar validation was cancelled by its caller.");
      }
      if (!index.read(reinterpret_cast<char*>(page_descriptor.data()),
                      page_descriptor.size())) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Sidecar page index descriptor is truncated.");
      }
      const auto first_chunk = page * kChunksPerPage;
      const auto chunks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
          kChunksPerPage, result_.chunk_count - first_chunk));
      const auto first_record = first_chunk * kRecordsPerChunk;
      validate_page_file(expected_.analysis_directory, page_descriptor.data(), page,
                         first_chunk, chunks, first_record);
      const auto page_path = ordinal_path("pages/page-", page, ".sgip");
      output_bytes = checked_add(output_bytes,
          64 + static_cast<std::uint64_t>(chunks) * 128);
      const auto page_bytes = read_bounded_file(
          expected_.analysis_directory / page_path, 64 + chunks * 128ULL);
      for (std::uint32_t local = 0; local < chunks; ++local) {
        if (cancellation_requested_ && cancellation_requested_()) {
          fail(OfflineAnalysisErrorCode::cancelled,
               "Sidecar validation was cancelled by its caller.");
        }
        const auto chunk_index = first_chunk + local;
        const auto first_record_in_chunk = chunk_index * kRecordsPerChunk;
        const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            kRecordsPerChunk, result_.feature_count - first_record_in_chunk));
        const auto* chunk_descriptor = page_bytes.data() + 64 + local * 128;
        const auto chunk_file = read_chunk_payload(
            expected_.analysis_directory, chunk_descriptor, chunk_index,
            first_record_in_chunk, count, record_bytes, expected_.feature_mask,
            expected_.sample_count);
        output_bytes = checked_add(output_bytes, chunk_file.size());
      }
    }
    if (index.peek() != std::char_traits<char>::eof()) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar page index has trailing bytes.");
    }
    std::uint64_t observed_chunks = 0;
    std::uint64_t observed_pages = 0;
    for (std::filesystem::recursive_directory_iterator it(
             expected_.analysis_directory), end; it != end; ++it) {
      const auto status = it->symlink_status(error);
      if (error || std::filesystem::is_symlink(status)) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Sidecar package contains a linked entry.");
      }
      if (std::filesystem::is_regular_file(status)) {
        const auto relative = std::filesystem::relative(
            it->path(), expected_.analysis_directory, error).generic_string();
        const bool expected_root_file = relative == "metadata.json" ||
                                        relative == "pages.sgai";
        const bool expected_chunk = is_expected_ordinal_path(
            relative, "chunks/chunk-", ".sgav", result_.chunk_count);
        const bool expected_page = is_expected_ordinal_path(
            relative, "pages/page-", ".sgip", result_.page_count);
        if (error || (!expected_root_file && !expected_chunk && !expected_page)) {
          fail(OfflineAnalysisErrorCode::output_validation_failed,
               "Sidecar package contains an unexpected file.");
        }
        observed_chunks += expected_chunk ? 1 : 0;
        observed_pages += expected_page ? 1 : 0;
      } else if (!std::filesystem::is_directory(status)) {
        fail(OfflineAnalysisErrorCode::output_validation_failed,
             "Sidecar package contains a non-file entry.");
      } else {
        const auto relative = std::filesystem::relative(
            it->path(), expected_.analysis_directory, error).generic_string();
        if (error || (relative != "chunks" && relative != "pages")) {
          fail(OfflineAnalysisErrorCode::output_validation_failed,
               "Sidecar package contains an unexpected directory.");
        }
      }
    }
    if (observed_chunks != result_.chunk_count ||
        observed_pages != result_.page_count) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar package is missing an expected chunk or page file.");
    }
    if (output_bytes > expected_.max_output_bytes) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar package is incomplete or exceeds its admitted output cap.");
    }
    result_.output_bytes = output_bytes;
  } catch (const OfflineAnalysisError&) {
    throw;
  } catch (const std::exception&) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Sidecar reader could not validate the package.");
  }
}

const OfflineSidecarResult& OfflineSidecarReader::result() const noexcept {
  return result_;
}

std::uint64_t OfflineSidecarReader::working_bytes() const noexcept {
  return reader_working_bytes(expected_.feature_mask);
}

std::vector<std::uint8_t> OfflineSidecarReader::read_record_bytes(
    std::uint64_t record_index) const {
  if (record_index >= result_.feature_count) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Requested sidecar record is outside the source range.");
  }
  try {
    if (cancellation_requested_ && cancellation_requested_()) {
      fail(OfflineAnalysisErrorCode::cancelled,
           "Sidecar read was cancelled by its caller.");
    }
    std::error_code error;
    const auto index_path = expected_.analysis_directory / "pages.sgai";
    const auto status = std::filesystem::symlink_status(index_path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status) ||
        sha256_file(index_path) != result_.page_index_sha256) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Sidecar index changed after reader admission.");
    }
    const auto chunk_index = record_index / kRecordsPerChunk;
    const auto page_index = chunk_index / kChunksPerPage;
    const auto local_chunk = chunk_index % kChunksPerPage;
    std::ifstream index(expected_.analysis_directory / "pages.sgai",
                        std::ios::binary);
    std::array<std::uint8_t, 128> page_descriptor{};
    index.seekg(static_cast<std::streamoff>(64 + page_index * 128));
    if (!index || !index.read(reinterpret_cast<char*>(page_descriptor.data()),
                              page_descriptor.size())) {
      fail(OfflineAnalysisErrorCode::output_validation_failed,
           "Requested sidecar page descriptor could not be read.");
    }
    const auto page_path = ordinal_path("pages/page-", page_index, ".sgip");
    const auto page_count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        kChunksPerPage, result_.chunk_count - page_index * kChunksPerPage));
    validate_page_file(expected_.analysis_directory, page_descriptor.data(), page_index,
                       page_index * kChunksPerPage, page_count,
                       page_index * kChunksPerPage * kRecordsPerChunk);
    const auto page = read_bounded_file(
        expected_.analysis_directory / page_path, 64 + page_count * 128ULL);
    const auto* chunk_descriptor = page.data() + 64 + local_chunk * 128;
    const auto first_record = chunk_index * kRecordsPerChunk;
    const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        kRecordsPerChunk, result_.feature_count - first_record));
    const auto chunk_file = read_chunk_payload(
        expected_.analysis_directory, chunk_descriptor, chunk_index,
        first_record, count,
        static_cast<std::uint32_t>(visual_analysis_record_bytes(
            expected_.feature_mask)), expected_.feature_mask,
        expected_.sample_count);
    const auto offset = static_cast<std::size_t>(record_index - first_record) *
        visual_analysis_record_bytes(expected_.feature_mask);
    auto result = std::vector<std::uint8_t>(
        chunk_file.begin() + static_cast<std::ptrdiff_t>(64 + offset),
        chunk_file.begin() + static_cast<std::ptrdiff_t>(
            64 + offset + visual_analysis_record_bytes(expected_.feature_mask)));
    return result;
  } catch (const OfflineAnalysisError&) {
    throw;
  } catch (const std::exception&) {
    fail(OfflineAnalysisErrorCode::output_validation_failed,
         "Requested sidecar record could not be read.");
  }
}

}  // namespace shipglows::audio
