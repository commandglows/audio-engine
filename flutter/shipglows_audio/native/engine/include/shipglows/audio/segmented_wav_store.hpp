#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "shipglows/audio/audio_types.hpp"

namespace shipglows::audio {

struct WavSegmentRecord final {
  std::uint32_t index = 0;
  std::uint64_t start_frame = 0;
  std::uint64_t frames = 0;
  std::uint64_t file_bytes = 0;
  std::string sha256;
  std::string file_name;
};

class SegmentedWavStore final {
 public:
  SegmentedWavStore(std::filesystem::path session_directory,
                    AudioFormat format, std::uint64_t frames_per_segment);
  ~SegmentedWavStore();

  SegmentedWavStore(const SegmentedWavStore&) = delete;
  SegmentedWavStore& operator=(const SegmentedWavStore&) = delete;

  void append(std::span<const std::byte> bytes);
  void finalize();

  [[nodiscard]] const std::vector<WavSegmentRecord>& segments() const noexcept;
  [[nodiscard]] std::uint64_t total_frames() const noexcept;

 private:
  void open_next_segment();
  void finalize_current_segment();
  void append_journal(const std::string& record);

  std::filesystem::path directory_;
  AudioFormat format_;
  std::uint64_t frames_per_segment_;
  std::uint64_t current_frames_ = 0;
  std::uint64_t total_frames_ = 0;
  std::ofstream current_stream_;
  std::ofstream journal_;
  std::vector<WavSegmentRecord> segments_;
  bool finalized_ = false;
};

}  // namespace shipglows::audio
