#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "shipglows/audio/audio_types.hpp"

namespace shipglows::audio {

enum class SessionEvent : std::uint8_t {
  pause,
  resume,
  interruption,
  route_change,
  device_restart,
  warning,
  failure,
};

struct WavSegmentRecord final {
  std::uint32_t index = 0;
  std::uint64_t start_frame = 0;
  std::uint64_t frames = 0;
  std::uint64_t file_bytes = 0;
  std::string sha256;
  std::string file_name;
};

struct WavStoreFaultPolicy final {
  std::uint64_t fail_after_frames = 0;
};

class SegmentedWavStore final {
 public:
  SegmentedWavStore(std::filesystem::path session_directory,
                    AudioFormat format, std::uint64_t frames_per_segment,
                    WavStoreFaultPolicy fault_policy = {});
  ~SegmentedWavStore();

  SegmentedWavStore(const SegmentedWavStore&) = delete;
  SegmentedWavStore& operator=(const SegmentedWavStore&) = delete;

  void append(std::span<const std::byte> bytes);
  void checkpoint(SessionEvent event, std::string_view reason = {});
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
  WavStoreFaultPolicy fault_policy_{};
};

}  // namespace shipglows::audio
