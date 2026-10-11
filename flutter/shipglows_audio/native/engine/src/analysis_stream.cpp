#include "shipglows/audio/analysis_stream.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace shipglows::audio {

AnalysisStream::AnalysisStream(unsigned feature_mask,
                               VisualAnalysisConfig config,
                               std::uint32_t maximum_block_frames)
    : feature_mask_(feature_mask),
      maximum_block_frames_(maximum_block_frames),
      queue_(maximum_block_frames),
      processor_(feature_mask, config),
      scratch_(static_cast<std::size_t>(maximum_block_frames) * 2) {
  worker_ = std::thread(&AnalysisStream::worker_loop, this);
}

AnalysisStream::~AnalysisStream() { stop(false); }

bool AnalysisStream::submit(const float* interleaved_pcm,
                            std::uint32_t frames,
                            std::uint64_t position_frames,
                            bool render_failed,
                            bool submission_succeeded) noexcept {
  if (!accepting_.load(std::memory_order_acquire) || frames == 0 ||
      frames > maximum_block_frames_ ||
      position_frames > std::numeric_limits<std::uint64_t>::max() - frames) {
    callback_gap_pending_ = true;
    return false;
  }
  const bool position_gap = callback_has_position_ &&
      position_frames != callback_expected_position_;
  const bool discontinuity = callback_gap_pending_ || position_gap ||
      force_discontinuity_.exchange(false, std::memory_order_acq_rel);
  callback_expected_position_ = position_frames + frames;
  callback_has_position_ = true;
  if (!submission_succeeded) {
    callback_gap_pending_ = true;
    return false;
  }
  const bool admitted = render_failed
      ? queue_.try_push_silence(frames, position_frames, discontinuity)
      : queue_.try_push(interleaved_pcm, frames, position_frames, discontinuity, false);
  callback_gap_pending_ = !admitted;
  return admitted;
}

void AnalysisStream::mark_discontinuity() noexcept {
  force_discontinuity_.store(true, std::memory_order_release);
}

AnalysisBatchSnapshot AnalysisStream::poll(std::uint64_t after_sequence) {
  std::lock_guard<std::mutex> lock(processor_mutex_);
  AnalysisBatchSnapshot result;
  const auto& stats = processor_.stats();
  if (after_sequence > stats.records) {
    throw std::invalid_argument("analysis cursor is ahead of the producer");
  }
  const auto count = processor_.history_count();
  result.has_origin = processor_.has_origin();
  result.origin_frame = processor_.origin_frame();
  result.continuity_id = processor_.continuity_id();
  result.next_sequence_cursor = after_sequence;
  result.dropped_pcm_frames = dropped_pcm_frames_;
  result.pcm_gap = pcm_gap_pending_;

  if (worker_failed_.load(std::memory_order_acquire)) {
    result.state = "failed";
  } else if (stopping_.load(std::memory_order_acquire)) {
    result.state = clean_eof_requested_.load(std::memory_order_acquire)
        ? "ended"
        : "gap";
  } else if (result.has_origin) {
    result.state = "live";
  }

  const bool continuity_changed = result.continuity_id != 0 &&
      ((reported_continuity_ != 0 &&
        result.continuity_id != reported_continuity_) ||
       (count != 0 &&
        (processor_.history(0).flags & 1u) != 0 &&
        installed_continuity_ != result.continuity_id));
  std::size_t first = 0;
  while (first < count && processor_.history(first).sequence <= after_sequence) {
    ++first;
  }
  const auto newer = count - first;
  const bool can_expect_next =
      after_sequence != std::numeric_limits<std::uint64_t>::max();
  const auto expected_next = can_expect_next ? after_sequence + 1 : after_sequence;
  std::string reason = "none";
  if (continuity_changed) {
    reason = "pcm_discontinuity";
  } else if (newer != 0 && first == 0 &&
             can_expect_next &&
             processor_.history(0).sequence > expected_next) {
    reason = "history_overwrite";
  } else if (newer > 16) {
    reason = initial_poll_ ? "initial_window" : "lag_window";
  }

  if (newer != 0) {
    if (reason != "none") {
      first = count > 8 ? count - 8 : 0;
      result.delivery_resync = true;
      result.resync_reason = reason;
    }
    const auto end = std::min(count, first + 16);
    result.records.reserve(end - first);
    for (auto index = first; index < end; ++index) {
      result.records.push_back(processor_.history(index));
    }
    if (!result.records.empty()) {
      result.first_sequence = result.records.front().sequence;
      result.last_sequence = result.records.back().sequence;
      result.next_sequence_cursor = result.last_sequence;
      result.skipped_delivery_records =
          can_expect_next && result.first_sequence > expected_next
              ? result.first_sequence - expected_next
              : 0;
      installed_continuity_ = result.continuity_id;
    }
  } else if (continuity_changed) {
    result.delivery_resync = true;
    result.resync_reason = reason;
  }
  reported_continuity_ = result.continuity_id;
  initial_poll_ = false;
  pcm_gap_pending_ = false;
  return result;
}

void AnalysisStream::stop_async(bool clean_eof) noexcept {
  accepting_.store(false, std::memory_order_release);
  if (clean_eof) clean_eof_requested_.store(true, std::memory_order_release);
  stopping_.store(true, std::memory_order_release);
}

void AnalysisStream::stop(bool clean_eof) noexcept {
  stop_async(clean_eof);
  if (worker_.joinable()) worker_.join();
}

std::uint64_t AnalysisStream::queue_drops() const noexcept {
  return queue_.drops();
}

std::uint64_t AnalysisStream::queue_high_water() const noexcept {
  return queue_.high_water();
}

std::uint32_t AnalysisStream::maximum_block_frames() const noexcept {
  return maximum_block_frames_;
}

std::size_t AnalysisStream::bytes() const noexcept {
  // Queue and scratch vectors own the only dynamic buffers; processor history
  // and all synchronization state are already inside sizeof(*this).
  return sizeof(*this) + queue_.bytes() +
      scratch_.capacity() * sizeof(float);
}

void AnalysisStream::worker_loop() noexcept {
  AnalysisBlockQueue::Header header{};
  for (;;) {
    if (queue_.try_pop(scratch_.data(), header)) {
      std::uint64_t end_position = header.position + header.frames;
      std::lock_guard<std::mutex> lock(processor_mutex_);
      if (worker_has_position_ && header.position > worker_expected_position_) {
        const auto missing = header.position - worker_expected_position_;
        dropped_pcm_frames_ = missing >
                std::numeric_limits<std::uint64_t>::max() - dropped_pcm_frames_
            ? std::numeric_limits<std::uint64_t>::max()
            : dropped_pcm_frames_ + missing;
        pcm_gap_pending_ = true;
      } else if (worker_has_position_ &&
                 header.position != worker_expected_position_) {
        pcm_gap_pending_ = true;
      }
      try {
        processor_.push(scratch_.data(), header.frames, header.position,
                        header.discontinuity, header.submitted_silence);
        worker_expected_position_ = end_position;
        worker_has_position_ = true;
      } catch (...) {
        worker_failed_.store(true, std::memory_order_release);
        accepting_.store(false, std::memory_order_release);
        stopping_.store(true, std::memory_order_release);
      }
      if (worker_failed_.load(std::memory_order_acquire)) break;
      continue;
    }
    if (stopping_.load(std::memory_order_acquire)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  if (!worker_failed_.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(processor_mutex_);
    try {
      if (clean_eof_requested_.load(std::memory_order_acquire)) {
        processor_.finish();
      } else {
        processor_.end_gap();
      }
    } catch (...) {
      worker_failed_.store(true, std::memory_order_release);
    }
  }
}

}  // namespace shipglows::audio
