#include "shipglows/audio/offline_wav_analysis.hpp"

#include "shipglows/audio/sha256.hpp"
#include "shipglows/audio/visual_analysis_wire.hpp"
#include "shipglows/audio/visual_transform_contract.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <span>
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

void put_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void put_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void patch_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes[offset + shift / 8] = static_cast<std::uint8_t>(value >> shift);
  }
}

std::vector<std::uint8_t> make_wav(const std::vector<float>& pcm,
                                   std::uint16_t format_tag = 3,
                                   std::size_t ancillary_bytes = 0) {
  std::vector<std::uint8_t> bytes;
  bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
  put_u32(bytes, 0);
  bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  put_u32(bytes, 16);
  put_u16(bytes, format_tag);
  put_u16(bytes, 2);
  put_u32(bytes, 48000);
  put_u32(bytes, 384000);
  put_u16(bytes, 8);
  put_u16(bytes, 32);
  if (ancillary_bytes != 0) {
    bytes.insert(bytes.end(), {'J', 'U', 'N', 'K'});
    put_u32(bytes, static_cast<std::uint32_t>(ancillary_bytes));
    bytes.resize(bytes.size() + ancillary_bytes, 0x5a);
    if ((ancillary_bytes & 1U) != 0) bytes.push_back(0);
  }
  bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
  put_u32(bytes, static_cast<std::uint32_t>(pcm.size() * 4));
  for (const float value : pcm) put_u32(bytes, std::bit_cast<std::uint32_t>(value));
  patch_u32(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
  return bytes;
}

std::string sha256(const std::vector<std::uint8_t>& bytes) {
  Sha256 hash;
  hash.update(std::as_bytes(std::span(bytes)));
  return Sha256::hex(hash.finalize());
}

OfflineWavRequest request_for(const std::vector<std::uint8_t>& wav,
                              std::uint64_t frames) {
  return OfflineWavRequest{frames, 48000 * 60, 8 * 1024 * 1024, 257,
                           sha256(wav)};
}

void expect_error(const std::vector<std::uint8_t>& wav,
                  OfflineWavRequest request, unsigned mask,
                  OfflineAnalysisErrorCode expected,
                  const char* message) {
  std::string content(reinterpret_cast<const char*>(wav.data()), wav.size());
  std::istringstream input(content, std::ios::binary | std::ios::in);
  bool caught = false;
  try {
    (void)analyze_offline_wav(input, mask, VisualAnalysisConfig{}, request,
                              [](const VisualAnalysisRecord&) {});
  } catch (const OfflineAnalysisError& error) {
    caught = error.code() == expected;
  }
  expect(caught, message);
}

void test_streams_all_profiles_in_bounded_blocks() {
  constexpr std::uint64_t frames = 777;
  std::vector<float> pcm(frames * 2, 0.0F);
  for (std::size_t index = 0; index < pcm.size(); ++index) {
    pcm[index] = static_cast<float>(.2 * std::sin(index * .017));
  }
  const auto wav = make_wav(pcm);
  const std::string content(reinterpret_cast<const char*>(wav.data()), wav.size());
  std::vector<std::vector<std::uint8_t>> reference_records;
  for (const unsigned mask : {3u, 5u, 11u, 13u}) {
    std::istringstream input(content, std::ios::binary | std::ios::in);
    auto request = request_for(wav, frames);
    std::vector<VisualAnalysisRecord> records;
    const auto result = analyze_offline_wav(
        input, mask, VisualAnalysisConfig{}, request,
        [&records](const VisualAnalysisRecord& record) { records.push_back(record); });
    expect(result.sample_count == frames && result.feature_count == 2,
           "offline WAV result reports source frames and emitted hops");
    expect(result.input_sha256 == request.input_sha256 &&
               result.record_bytes == visual_analysis_record_bytes(mask),
           "offline WAV result binds digest and compact profile layout");
    expect(records.size() == 2 && records[0].sequence == 1 &&
               records[0].center == 0 && records[0].valid_frames == 480 &&
               records[1].sequence == 2 && records[1].center == 480 &&
               records[1].valid_frames == 297 && records[0].flags == 0 &&
               records[1].flags == 0,
           "offline WAV records preserve clean origin and final partial hop");
    expect(result.stats.fft_calls == ((mask & 8) != 0 ? 4u : 0u),
           "offline WAV only calculates spectrum when requested");
    if (mask == 3) {
      for (const auto& record : records) {
        reference_records.push_back(encode_visual_analysis_record(record, mask));
      }
    }
  }

  auto single_frame_request = request_for(wav, frames);
  single_frame_request.block_frames = 1;
  std::istringstream single_frame_input(content,
                                       std::ios::binary | std::ios::in);
  std::vector<std::vector<std::uint8_t>> single_frame_records;
  (void)analyze_offline_wav(
      single_frame_input, 3, VisualAnalysisConfig{}, single_frame_request,
      [&single_frame_records](const VisualAnalysisRecord& record) {
        single_frame_records.push_back(encode_visual_analysis_record(record, 3));
      });
  expect(single_frame_records == reference_records,
         "offline WAV output is independent of the admitted read partition");
}

void test_closed_format_digest_duration_and_memory_admission() {
  const auto wav = make_wav(std::vector<float>(64, 0.0F));
  auto request = request_for(wav, 32);
  request.input_sha256[0] = request.input_sha256[0] == '0' ? '1' : '0';
  expect_error(wav, request, 5, OfflineAnalysisErrorCode::invalid_media,
               "offline WAV rejects a manifest digest mismatch");

  request = request_for(wav, 32);
  request.sample_count = 31;
  expect_error(wav, request, 5, OfflineAnalysisErrorCode::invalid_media,
               "offline WAV rejects a declared frame-count mismatch");

  request = request_for(wav, 32);
  request.max_duration_frames = 31;
  expect_error(wav, request, 5,
               OfflineAnalysisErrorCode::resource_limit_exceeded,
               "offline WAV enforces the explicit duration limit");

  request = request_for(wav, 32);
  request.max_memory_bytes = 1;
  expect_error(wav, request, 5,
               OfflineAnalysisErrorCode::resource_limit_exceeded,
               "offline WAV enforces the explicit working-memory limit");

  const auto compressed = make_wav(std::vector<float>(64, 0.0F), 1);
  expect_error(compressed, request_for(compressed, 32), 5,
               OfflineAnalysisErrorCode::unsupported_media,
               "offline WAV rejects integer PCM");

  const auto oversized_metadata = make_wav(
      std::vector<float>(64, 0.0F), 3, 1024 * 1024);
  expect_error(oversized_metadata, request_for(oversized_metadata, 32), 5,
               OfflineAnalysisErrorCode::resource_limit_exceeded,
               "offline WAV caps ancillary metadata overhead");
}

void test_nonfinite_pcm_is_rejected() {
  auto pcm = std::vector<float>(64, 0.0F);
  pcm[7] = std::numeric_limits<float>::quiet_NaN();
  const auto wav = make_wav(pcm);
  expect_error(wav, request_for(wav, 32), 5,
               OfflineAnalysisErrorCode::invalid_pcm,
               "offline WAV rejects non-finite PCM before publication");
}

void test_cancellation_is_observed_while_hashing() {
  const auto wav = make_wav(std::vector<float>(64, 0.0F));
  auto request = request_for(wav, 32);
  std::uint32_t checks = 0;
  request.cancellation_requested = [&checks] { return ++checks == 3; };
  expect_error(wav, request, 5, OfflineAnalysisErrorCode::cancelled,
               "offline WAV stops when its caller cancels during validation");
  expect(checks == 3,
         "offline WAV observes cancellation at a bounded validation point");
}

void test_shared_transform_contract_hash() {
  const auto canonical = visual_transform_config_json(VisualAnalysisConfig{});
  expect(canonical ==
             "{\"amplitude_gamma\":0.8,\"attack_ms\":20,\"release_ms\":180,"
             "\"sensitivity_db\":0,\"spectrum_ceiling_db\":0,"
             "\"spectrum_floor_db\":-80,\"version\":1}",
         "native transform config uses the same canonical defaults as Dart");
  expect(visual_transform_config_digest(VisualAnalysisConfig{}) ==
             "74dc6923a924808157182ff39fcd281ef9560184235e4b48d084341f47784ef5",
         "native transform digest matches the Dart golden value");
  auto scientific = VisualAnalysisConfig{};
  scientific.sensitivity_db = 1e-7;
  expect(visual_transform_config_json(scientific).find(
             "\"sensitivity_db\":1e-7") != std::string::npos,
         "native transform config preserves canonical scientific notation");
}

}  // namespace

int main() {
  test_streams_all_profiles_in_bounded_blocks();
  test_closed_format_digest_duration_and_memory_admission();
  test_nonfinite_pcm_is_rejected();
  test_cancellation_is_observed_while_hashing();
  test_shared_transform_contract_hash();
}
