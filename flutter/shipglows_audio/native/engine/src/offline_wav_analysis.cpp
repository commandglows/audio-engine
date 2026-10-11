#include "shipglows/audio/offline_wav_analysis.hpp"

#include "shipglows/audio/sha256.hpp"
#include "shipglows/audio/visual_analysis_wire.hpp"
#include "shipglows/audio/visual_transform_contract.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>

namespace shipglows::audio {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t) &&
                  std::numeric_limits<float>::is_iec559,
              "Offline WAV analysis requires IEEE-754 binary32 floats.");

constexpr std::uint64_t kMaxRiffBytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 8;
constexpr std::uint64_t kMaxAncillaryBytes = 1024 * 1024;
constexpr std::size_t kHashBlockBytes = 64 * 1024;

struct WavLayout final {
  std::uint64_t file_bytes = 0;
  std::uint64_t data_offset = 0;
  std::uint64_t data_bytes = 0;
  std::uint64_t data_end = 0;
  std::uint64_t sample_count = 0;
  std::string digest;
};

[[noreturn]] void fail(OfflineAnalysisErrorCode code, const char* message) {
  throw OfflineAnalysisError(code, message);
}

void read_exact(std::istream& input, char* destination, std::size_t bytes,
                OfflineAnalysisErrorCode code =
                    OfflineAnalysisErrorCode::invalid_media) {
  if (bytes == 0) return;
  input.read(destination, static_cast<std::streamsize>(bytes));
  if (input.gcount() != static_cast<std::streamsize>(bytes)) {
    fail(code, "WAV stream ended before its declared length.");
  }
}

std::uint16_t u16(const std::array<unsigned char, 16>& bytes,
                  std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
}

std::uint32_t u32(const unsigned char* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool is_lower_sha256(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

void update_hash(Sha256& hash, const void* data, std::size_t bytes) {
  hash.update(std::as_bytes(
      std::span(static_cast<const char*>(data), bytes)));
}

void cancellation_point(const std::function<bool()>& requested) {
  if (requested && requested()) {
    fail(OfflineAnalysisErrorCode::cancelled,
         "Offline analysis was cancelled by its caller.");
  }
}

void skip_and_hash(std::istream& input, std::uint64_t bytes, Sha256& hash,
                   const std::function<bool()>& cancellation_requested) {
  std::array<char, kHashBlockBytes> block{};
  while (bytes != 0) {
    cancellation_point(cancellation_requested);
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(bytes, block.size()));
    read_exact(input, block.data(), count);
    update_hash(hash, block.data(), count);
    bytes -= count;
  }
}

WavLayout validate_and_hash(
    std::istream& input,
    const std::function<bool()>& cancellation_requested) {
  cancellation_point(cancellation_requested);
  input.clear();
  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < 12) fail(OfflineAnalysisErrorCode::invalid_media,
                     "WAV file is shorter than its RIFF header.");
  const auto file_bytes = static_cast<std::uint64_t>(end);
  if (file_bytes > kMaxRiffBytes) {
    fail(OfflineAnalysisErrorCode::unsupported_media,
         "RIFF/WAVE exceeds its 32-bit container limit.");
  }
  input.clear();
  input.seekg(0, std::ios::beg);
  if (!input) fail(OfflineAnalysisErrorCode::invalid_media,
                   "WAV stream must be seekable.");

  Sha256 hash;
  std::array<unsigned char, 12> header{};
  read_exact(input, reinterpret_cast<char*>(header.data()), header.size());
  update_hash(hash, header.data(), header.size());
  if (std::memcmp(header.data(), "RIFF", 4) != 0 ||
      std::memcmp(header.data() + 8, "WAVE", 4) != 0 ||
      static_cast<std::uint64_t>(u32(header.data() + 4)) + 8 != file_bytes) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "WAV RIFF identity or container length is invalid.");
  }

  bool have_format = false;
  bool have_data = false;
  std::uint64_t position = 12;
  std::uint64_t ancillary_bytes = 0;
  std::uint64_t data_offset = 0;
  std::uint64_t data_bytes = 0;
  while (position < file_bytes) {
    cancellation_point(cancellation_requested);
    if (file_bytes - position < 8) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "WAV contains a truncated chunk header.");
    }
    std::array<unsigned char, 8> chunk{};
    read_exact(input, reinterpret_cast<char*>(chunk.data()), chunk.size());
    update_hash(hash, chunk.data(), chunk.size());
    position += chunk.size();
    const auto length = static_cast<std::uint64_t>(u32(chunk.data() + 4));
    const auto padded_length = length + (length & 1U);
    if (padded_length > file_bytes - position) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "WAV chunk exceeds the RIFF container boundary.");
    }
    const bool is_format = std::memcmp(chunk.data(), "fmt ", 4) == 0;
    const bool is_data = std::memcmp(chunk.data(), "data", 4) == 0;
    if (is_format) {
      if (have_format || length != 16) {
        fail(OfflineAnalysisErrorCode::unsupported_media,
             "Only one canonical 16-byte WAV format chunk is supported.");
      }
      std::array<unsigned char, 16> format{};
      read_exact(input, reinterpret_cast<char*>(format.data()), format.size());
      update_hash(hash, format.data(), format.size());
      position += format.size();
      if (u16(format, 0) != 3 || u16(format, 2) != 2 ||
          u32(format.data() + 4) != 48000 ||
          u32(format.data() + 8) != 384000 || u16(format, 12) != 8 ||
          u16(format, 14) != 32) {
        fail(OfflineAnalysisErrorCode::unsupported_media,
             "WAV must contain IEEE float32 stereo PCM at 48 kHz.");
      }
      have_format = true;
    } else if (is_data) {
      if (have_data || (length % 8) != 0) {
        fail(OfflineAnalysisErrorCode::invalid_media,
             "WAV must contain one frame-aligned data chunk.");
      }
      data_offset = position;
      data_bytes = length;
      skip_and_hash(input, length, hash, cancellation_requested);
      position += length;
      have_data = true;
    } else {
      const auto cost = static_cast<std::uint64_t>(chunk.size()) + padded_length;
      if (cost > kMaxAncillaryBytes - ancillary_bytes) {
        fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
             "WAV ancillary chunks exceed the 1 MiB admission limit.");
      }
      ancillary_bytes += cost;
      skip_and_hash(input, length, hash, cancellation_requested);
      position += length;
    }
    if ((length & 1U) != 0) {
      char padding = 0;
      read_exact(input, &padding, 1);
      update_hash(hash, &padding, 1);
      ++position;
    }
  }
  if (position != file_bytes || !have_format || !have_data) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "WAV is missing its unique format or data chunk.");
  }
  return WavLayout{file_bytes, data_offset, data_bytes, data_offset + data_bytes,
                   data_bytes / 8, Sha256::hex(hash.finalize())};
}

void drain_records(const VisualAnalysisProcessor& processor,
                   std::uint64_t& delivered,
                   const std::function<void(const VisualAnalysisRecord&)>& sink) {
  for (std::size_t index = 0; index < processor.history_count(); ++index) {
    const auto& record = processor.history(index);
    if (record.sequence <= delivered) continue;
    if (record.sequence != delivered + 1) {
      fail(OfflineAnalysisErrorCode::analysis_failed,
           "Offline analyzer history overflowed before records were delivered.");
    }
    sink(record);
    delivered = record.sequence;
  }
}

}  // namespace

OfflineAnalysisError::OfflineAnalysisError(OfflineAnalysisErrorCode code,
                                           const char* message)
    : std::runtime_error(message), code_(code) {}

std::uint64_t offline_wav_analysis_working_bytes(std::uint32_t block_frames) {
  if (block_frames == 0 || block_frames > 16384) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Offline WAV block size exceeds its admission ceiling.");
  }
  constexpr std::uint64_t kAllocatorAllowanceBytes = 256;
  const auto block_bytes = static_cast<std::uint64_t>(block_frames) * 8;
  return static_cast<std::uint64_t>(sizeof(VisualAnalysisProcessor)) +
         sizeof(VisualAnalysisProcessor*) + block_bytes * 2 + kHashBlockBytes +
         kAllocatorAllowanceBytes;
}

OfflineWavResult analyze_offline_wav(
    std::istream& input, unsigned feature_mask,
    const VisualAnalysisConfig& transform, const OfflineWavRequest& request,
    const std::function<void(const VisualAnalysisRecord&)>& record_sink) {
  if (!record_sink) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Offline analysis requires a record sink.");
  }
  if (!is_lower_sha256(request.input_sha256) ||
      request.max_duration_frames == 0 || request.max_memory_bytes == 0 ||
      request.block_frames == 0 || request.block_frames > 16384) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Offline analysis limits, digest, or block size are invalid.");
  }

  const auto layout = validate_and_hash(input, request.cancellation_requested);
  if (layout.digest != request.input_sha256 ||
      layout.sample_count != request.sample_count) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "WAV digest or frame count does not match the admitted request.");
  }
  if (layout.sample_count > request.max_duration_frames) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "WAV duration exceeds its explicit frame budget.");
  }

  const auto estimated_bytes =
      offline_wav_analysis_working_bytes(request.block_frames);
  const auto block_bytes = static_cast<std::uint64_t>(request.block_frames) * 8;
  if (estimated_bytes > request.max_memory_bytes) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Offline analysis exceeds its explicit working-memory budget.");
  }
  std::unique_ptr<VisualAnalysisProcessor> processor;
  try {
    processor = std::make_unique<VisualAnalysisProcessor>(feature_mask, transform);
  } catch (const std::invalid_argument&) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "Offline profile, mask, or transform configuration is invalid.");
  } catch (const std::bad_alloc&) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Offline analyzer context could not be admitted in memory.");
  }

  input.clear();
  input.seekg(0, std::ios::beg);
  Sha256 second_hash;
  std::array<char, kHashBlockBytes> io{};
  std::uint64_t prefix_remaining = layout.data_offset;
  while (prefix_remaining != 0) {
    cancellation_point(request.cancellation_requested);
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(prefix_remaining, io.size()));
    read_exact(input, io.data(), count, OfflineAnalysisErrorCode::input_changed);
    update_hash(second_hash, io.data(), count);
    prefix_remaining -= count;
  }

  const auto frames_per_block = static_cast<std::size_t>(request.block_frames);
  std::unique_ptr<unsigned char[]> raw(
      new (std::nothrow) unsigned char[static_cast<std::size_t>(block_bytes)]);
  std::unique_ptr<float[]> pcm(new (std::nothrow) float[frames_per_block * 2]);
  if (!raw || !pcm) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Offline analysis buffers could not be admitted in memory.");
  }
  std::uint64_t frames_read = 0;
  std::uint64_t delivered = 0;
  while (frames_read < layout.sample_count) {
    cancellation_point(request.cancellation_requested);
    const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(
        request.block_frames, layout.sample_count - frames_read));
    const auto bytes = frames * 8;
    read_exact(input, reinterpret_cast<char*>(raw.get()), bytes,
               OfflineAnalysisErrorCode::input_changed);
    update_hash(second_hash, raw.get(), bytes);
    for (std::size_t sample = 0; sample < frames * 2; ++sample) {
      const auto bits = u32(raw.get() + sample * 4);
      pcm[sample] = std::bit_cast<float>(bits);
    }
    try {
      processor->push(pcm.get(), frames, frames_read);
    } catch (const std::invalid_argument&) {
      fail(OfflineAnalysisErrorCode::invalid_pcm,
           "WAV contains non-finite or out-of-range float32 PCM.");
    }
    frames_read += frames;
    drain_records(*processor, delivered, record_sink);
  }

  std::uint64_t suffix_remaining = layout.file_bytes - layout.data_end;
  while (suffix_remaining != 0) {
    cancellation_point(request.cancellation_requested);
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(suffix_remaining, io.size()));
    read_exact(input, io.data(), count, OfflineAnalysisErrorCode::input_changed);
    update_hash(second_hash, io.data(), count);
    suffix_remaining -= count;
  }
  const auto second_digest = Sha256::hex(second_hash.finalize());
  if (second_digest != layout.digest || second_digest != request.input_sha256) {
    fail(OfflineAnalysisErrorCode::input_changed,
         "WAV changed between validation and analysis.");
  }

  try {
    processor->finish();
  } catch (const std::exception&) {
    fail(OfflineAnalysisErrorCode::analysis_failed,
         "Offline analyzer failed while finalizing the stream.");
  }
  drain_records(*processor, delivered, record_sink);

  return OfflineWavResult{layout.sample_count,
                          delivered,
                          estimated_bytes,
                          static_cast<std::uint32_t>(
                              visual_analysis_record_bytes(feature_mask)),
                          second_digest,
                          processor->stats()};
}

OfflinePackageResult analyze_wav_to_sidecar(
    std::istream& input, unsigned feature_mask,
    const VisualAnalysisConfig& transform, OfflineWavRequest wav_request,
    OfflineSidecarConfig sidecar_config) {
  if (wav_request.sample_count != sidecar_config.sample_count ||
      wav_request.input_sha256 != sidecar_config.input_sha256 ||
      wav_request.max_memory_bytes == 0 ||
      wav_request.max_memory_bytes != sidecar_config.max_memory_bytes ||
      feature_mask != sidecar_config.feature_mask) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "WAV analysis and sidecar admission identities do not match.");
  }
  try {
    if (visual_transform_config_json(transform) !=
        visual_transform_config_json(sidecar_config.transform)) {
      fail(OfflineAnalysisErrorCode::invalid_media,
           "WAV analysis and sidecar transform configurations differ.");
    }
  } catch (const OfflineAnalysisError&) {
    throw;
  } catch (const std::exception&) {
    fail(OfflineAnalysisErrorCode::invalid_media,
         "WAV analysis and sidecar transform configuration is invalid.");
  }
  const auto total_working_bytes =
      offline_wav_analysis_working_bytes(wav_request.block_frames) +
      offline_sidecar_working_bytes(feature_mask);
  if (total_working_bytes > sidecar_config.max_memory_bytes) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Combined WAV analyzer and sidecar buffers exceed the memory budget.");
  }
  const auto required_output =
      offline_sidecar_output_reserve(sidecar_config.sample_count, feature_mask);
  if (required_output > sidecar_config.max_output_bytes) {
    fail(OfflineAnalysisErrorCode::resource_limit_exceeded,
         "Sidecar output reserve exceeds the explicit output budget.");
  }
  wav_request.max_memory_bytes -=
      offline_sidecar_working_bytes(feature_mask);
  OfflineSidecarWriter writer(std::move(sidecar_config));
  const auto analysis = analyze_offline_wav(
      input, feature_mask, transform, wav_request,
      [&writer](const VisualAnalysisRecord& record) { writer.append(record); });
  const auto sidecar = writer.finish();
  return OfflinePackageResult{analysis, sidecar};
}

}  // namespace shipglows::audio
