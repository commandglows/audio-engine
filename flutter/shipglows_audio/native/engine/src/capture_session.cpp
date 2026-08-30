#include "shipglows/audio/capture_session.hpp"

namespace shipglows::audio {

bool CaptureSession::prepare(AudioFormat format) noexcept {
  if (!format.valid()) {
    return false;
  }
  auto expected = SessionState::idle;
  if (!state_.compare_exchange_strong(expected, SessionState::prepared)) {
    return false;
  }
  format_ = format;
  return true;
}

bool CaptureSession::start() noexcept {
  auto expected = SessionState::prepared;
  return state_.compare_exchange_strong(expected, SessionState::recording);
}

bool CaptureSession::request_stop() noexcept {
  auto expected = SessionState::recording;
  return state_.compare_exchange_strong(expected, SessionState::stopping);
}

bool CaptureSession::finish() noexcept {
  auto expected = SessionState::stopping;
  return state_.compare_exchange_strong(expected, SessionState::stopped);
}

void CaptureSession::fail() noexcept { state_.store(SessionState::failed); }

void CaptureSession::count_captured_frames(std::uint64_t frames) noexcept {
  frames_captured_.fetch_add(frames, std::memory_order_relaxed);
}

void CaptureSession::count_dropped_frames(std::uint64_t frames) noexcept {
  frames_dropped_.fetch_add(frames, std::memory_order_relaxed);
}

void CaptureSession::count_discontinuity() noexcept {
  discontinuities_.fetch_add(1, std::memory_order_relaxed);
}

void CaptureSession::count_clipped_samples(std::uint64_t samples) noexcept {
  clipped_samples_.fetch_add(samples, std::memory_order_relaxed);
}

void CaptureSession::count_device_restart() noexcept {
  device_restarts_.fetch_add(1, std::memory_order_relaxed);
}

SessionState CaptureSession::state() const noexcept { return state_.load(); }

AudioFormat CaptureSession::format() const noexcept { return format_; }

CaptureMetrics CaptureSession::metrics() const noexcept {
  return {
      .frames_captured = frames_captured_.load(std::memory_order_relaxed),
      .frames_dropped = frames_dropped_.load(std::memory_order_relaxed),
      .discontinuities = discontinuities_.load(std::memory_order_relaxed),
      .clipped_samples = clipped_samples_.load(std::memory_order_relaxed),
      .device_restarts = device_restarts_.load(std::memory_order_relaxed),
  };
}

}  // namespace shipglows::audio
