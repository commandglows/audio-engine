#pragma once

#include "shipglows/audio/visual_analysis.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace shipglows::audio {

struct AnalysisBatchSnapshot final {
  std::string state = "warming_up";
  bool has_origin = false;
  std::uint64_t continuity_id = 0;
  std::uint64_t origin_frame = 0;
  std::uint64_t first_sequence = 0;
  std::uint64_t last_sequence = 0;
  std::uint64_t next_sequence_cursor = 0;
  std::uint64_t skipped_delivery_records = 0;
  std::uint64_t dropped_pcm_frames = 0;
  bool delivery_resync = false;
  std::string resync_reason = "none";
  bool pcm_gap = false;
  std::vector<VisualAnalysisRecord> records;
};

/// Worker-owned streaming analysis with callback-safe bounded whole-block input.
/// Source identity, presentation clocks and wire leases belong to the adapter.
class AnalysisStream final {
 public:
  AnalysisStream(unsigned feature_mask, VisualAnalysisConfig config,
                 std::uint32_t maximum_block_frames);
  ~AnalysisStream();
  AnalysisStream(const AnalysisStream&) = delete;
  AnalysisStream& operator=(const AnalysisStream&) = delete;

  /// Called after the device reports submission. This method never allocates,
  /// locks, waits or invokes application code.
  [[nodiscard]] bool submit(const float* interleaved_pcm,
                             std::uint32_t frames,
                             std::uint64_t position_frames,
                             bool render_failed,
                             bool submission_succeeded) noexcept;
  void mark_discontinuity() noexcept;

  /// Cursor is a previously committed consumer cursor; future cursors reject.
  [[nodiscard]] AnalysisBatchSnapshot poll(std::uint64_t after_sequence);
  void stop_async(bool clean_eof) noexcept;
  void stop(bool clean_eof = false) noexcept;

  [[nodiscard]] std::uint64_t queue_drops() const noexcept;
  [[nodiscard]] std::uint64_t queue_high_water() const noexcept;
  [[nodiscard]] std::uint32_t maximum_block_frames() const noexcept;
  [[nodiscard]] std::size_t bytes() const noexcept;

 private:
  void worker_loop() noexcept;

  const unsigned feature_mask_;
  const std::uint32_t maximum_block_frames_;
  AnalysisBlockQueue queue_;
  VisualAnalysisProcessor processor_;
  std::vector<float> scratch_;
  std::atomic<bool> accepting_{true};
  std::atomic<bool> force_discontinuity_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> worker_failed_{false};
  std::atomic<bool> clean_eof_requested_{false};
  std::uint64_t callback_expected_position_ = 0;  // Single callback writer.
  bool callback_has_position_ = false;             // Single callback writer.
  bool callback_gap_pending_ = false;    // Single callback writer.
  std::thread worker_;
  mutable std::mutex processor_mutex_;
  bool worker_has_position_ = false;
  std::uint64_t worker_expected_position_ = 0;
  std::uint64_t dropped_pcm_frames_ = 0;
  bool pcm_gap_pending_ = false;
  std::uint64_t reported_continuity_ = 0;
  std::uint64_t installed_continuity_ = 0;
  bool initial_poll_ = true;
};

}  // namespace shipglows::audio
