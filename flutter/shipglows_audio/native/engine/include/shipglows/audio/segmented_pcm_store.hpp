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

struct SegmentRecord final {
  std::uint32_t index = 0;
  std::uint64_t frames = 0;
  std::string file_name;
};

struct RecoveryReport final {
  std::uint64_t recovered_frames = 0;
  std::uint64_t discarded_bytes = 0;
  std::vector<SegmentRecord> segments;
};

class SegmentedPcmStore final {
 public:
  SegmentedPcmStore(std::filesystem::path session_directory,
                    AudioFormat format, std::uint64_t frames_per_segment);
  ~SegmentedPcmStore();

  SegmentedPcmStore(const SegmentedPcmStore&) = delete;
  SegmentedPcmStore& operator=(const SegmentedPcmStore&) = delete;

  void append(std::span<const std::byte> bytes);
  void finalize();

  [[nodiscard]] const std::vector<SegmentRecord>& segments() const noexcept;
  [[nodiscard]] static RecoveryReport recover(
      const std::filesystem::path& session_directory, AudioFormat format);

 private:
  void open_next_segment();
  void finalize_current_segment();
  void write_manifest(bool complete) const;

  std::filesystem::path directory_;
  AudioFormat format_;
  std::uint64_t frames_per_segment_;
  std::uint64_t current_frames_ = 0;
  std::ofstream current_stream_;
  std::vector<SegmentRecord> segments_;
  bool finalized_ = false;
};

}  // namespace shipglows::audio
