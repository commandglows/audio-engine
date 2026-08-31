#include "android_oboe_capture.h"

#include <algorithm>
#include <chrono>
#include <span>
#include <sstream>
#include <vector>

#include "shipglows/audio/pcm_analysis.hpp"
#include "shipglows/audio/segmented_wav_store.hpp"
#include "shipglows/audio/recording_preflight.hpp"

namespace shipglows_audio {

AndroidOboeCapture::AndroidOboeCapture() = default;

AndroidOboeCapture::~AndroidOboeCapture() { Stop(); }

bool AndroidOboeCapture::Start(
    const std::filesystem::path& session_directory) {
  return Start(session_directory, oboe::Unspecified);
}

bool AndroidOboeCapture::Start(
    const std::filesystem::path& session_directory,
    std::int32_t input_device_id) {
  {
    std::lock_guard lock(mutex_);
    if (stream_ != nullptr ||
        session_.state() != shipglows::audio::SessionState::idle) {
      return false;
    }
  }

  const auto preflight = shipglows::audio::recording_preflight(session_directory);
  if (!preflight.ready) {
    SetError(preflight.error_code);
    return false;
  }

  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Input)
      ->setSharingMode(oboe::SharingMode::Shared)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setFormat(oboe::AudioFormat::I16)
      ->setFormatConversionAllowed(true)
      ->setInputPreset(oboe::InputPreset::Unprocessed)
      ->setDataCallback(this)
      ->setErrorCallback(this);
  if (input_device_id >= 0) {
    builder.setDeviceId(input_device_id);
  }
  input_device_id_.store(input_device_id, std::memory_order_relaxed);
  recovery_coordinator_.Reset();
  recovery_coordinator_.SetRoute(input_device_id, input_device_id >= 0);

  auto open_result = builder.openStream(stream_);
  if (open_result != oboe::Result::OK || stream_ == nullptr) {
    // Some Android devices expose an input path but reject the Unprocessed
    // preset. Keep the low-latency/shared contract and retry with the broadly
    // supported generic preset before treating the device as unavailable.
    stream_.reset();
    builder.setInputPreset(oboe::InputPreset::Generic);
    open_result = builder.openStream(stream_);
  }
  if (open_result != oboe::Result::OK || stream_ == nullptr) {
    SetError("oboe_open_failed");
    return false;
  }
  if (input_device_id >= 0 && stream_->getDeviceId() != input_device_id) {
    stream_->close();
    stream_.reset();
    SetError("input_device_unavailable");
    return false;
  }

  format_ = {
      .sample_rate = static_cast<std::uint32_t>(stream_->getSampleRate()),
      .channel_count = static_cast<std::uint16_t>(stream_->getChannelCount()),
      .sample_format = shipglows::audio::SampleFormat::int16,
  };
  if (!format_.valid() || stream_->getFormat() != oboe::AudioFormat::I16) {
    stream_->close();
    stream_.reset();
    SetError("oboe_format_unsupported");
    return false;
  }

  const auto ring_bytes = static_cast<std::size_t>(format_.sample_rate) *
                          format_.bytes_per_frame() * 10;
  ring_ = std::make_unique<
      shipglows::audio::SpscAudioRingBuffer<std::byte>>(ring_bytes);
  if (!session_.prepare(format_) || !session_.start()) {
    stream_->close();
    stream_.reset();
    SetError("session_state_failed");
    return false;
  }
  timestamp_tracker_.reset(format_.sample_rate,
                           lifecycle_generation_.load());

  capture_finished_.store(false);
  stop_requested_.store(false);
  paused_.store(false);
  storage_command_.store(0);
  storage_thread_ =
      std::thread(&AndroidOboeCapture::StorageWorker, this, session_directory);
  const auto start_result = stream_->requestStart();
  if (start_result != oboe::Result::OK) {
    session_.fail();
    capture_finished_.store(true);
    if (storage_thread_.joinable()) {
      storage_thread_.join();
    }
    stream_->close();
    stream_.reset();
    SetError("oboe_start_failed");
    return false;
  }
  return true;
}

void AndroidOboeCapture::Stop() {
  stop_requested_.store(true, std::memory_order_release);
  recovery_coordinator_.RequestStop();
  storage_command_condition_.notify_all();
  std::shared_ptr<oboe::AudioStream> stream;
  {
    std::lock_guard lock(mutex_);
    stream = stream_;
    if (session_.state() == shipglows::audio::SessionState::recording ||
        session_.state() == shipglows::audio::SessionState::paused) {
      static_cast<void>(session_.request_stop());
    }
  }
  if (stream != nullptr) {
    if (stream->isXRunCountSupported()) {
      const auto xruns = stream->getXRunCount();
      if (xruns && xruns.value() > 0) {
        session_.count_native_xruns(
            static_cast<std::uint64_t>(xruns.value()));
      }
    }
    stream->requestStop();
    stream->close();
  }
  capture_finished_.store(true, std::memory_order_release);
  if (storage_thread_.joinable()) {
    storage_thread_.join();
  }
  if (recovery_thread_.joinable()) {
    recovery_thread_.join();
  }
  std::lock_guard lock(mutex_);
  stream_.reset();
}

std::string AndroidOboeCapture::Pause() {
  if (!session_.pause()) {
    return StatusLine();
  }
  paused_.store(true, std::memory_order_release);
  if (!SubmitStorageCommand(1)) {
    SetError("pause_checkpoint_failed");
    session_.fail();
  }
  return StatusLine();
}

std::string AndroidOboeCapture::Resume() {
  if (session_.state() != shipglows::audio::SessionState::paused) {
    return StatusLine();
  }
  if (!SubmitStorageCommand(2)) {
    SetError("resume_checkpoint_failed");
    session_.fail();
    return StatusLine();
  }
  lifecycle_generation_.fetch_add(1, std::memory_order_relaxed);
  paused_.store(false, std::memory_order_release);
  static_cast<void>(session_.resume());
  return StatusLine();
}

std::string AndroidOboeCapture::SelectInputDevice(
    std::int32_t input_device_id) {
  if (input_device_id < 0 ||
      (session_.state() != shipglows::audio::SessionState::recording &&
       session_.state() != shipglows::audio::SessionState::paused)) {
    return StatusLine();
  }
  if (!SubmitStorageCommand(5)) {
    SetError("route_checkpoint_failed");
    session_.fail();
    return StatusLine();
  }
  input_device_id_.store(input_device_id, std::memory_order_relaxed);
  recovery_coordinator_.SetRoute(input_device_id, true);
  std::shared_ptr<oboe::AudioStream> previous;
  {
    std::lock_guard lock(mutex_);
    previous = std::move(stream_);
  }
  if (previous != nullptr) {
    previous->requestStop();
    previous->close();
  }
  session_.count_discontinuity();
  SetError("device_disconnected");
  if (OpenReplacementStream()) {
    lifecycle_generation_.fetch_add(1, std::memory_order_relaxed);
    session_.count_device_restart();
    session_.count_route_change();
    SetError({});
    static_cast<void>(SubmitStorageCommand(4));
  } else if (!recovery_thread_.joinable()) {
    recovery_thread_ = std::thread(&AndroidOboeCapture::RecoveryWorker, this);
  }
  return StatusLine();
}

void AndroidOboeCapture::SetRecoveryDevice(std::int32_t input_device_id,
                                           bool route_ready) {
  if (stop_requested_.load(std::memory_order_acquire)) {
    return;
  }
  if (route_ready && input_device_id >= 0) {
    input_device_id_.store(input_device_id, std::memory_order_release);
  }
  recovery_coordinator_.SetRoute(input_device_id,
                                 route_ready && input_device_id >= 0);
}

std::string AndroidOboeCapture::StatusLine() const {
  std::lock_guard lock(mutex_);
  const auto metrics = session_.metrics();
  std::ostringstream value;
  value << shipglows::audio::to_string(session_.state()) << '|'
        << format_.sample_rate << '|' << format_.channel_count << "|int16|"
        << metrics.frames_captured << '|' << metrics.frames_dropped << '|'
        << metrics.discontinuities << '|' << metrics.clipped_samples << '|'
        << metrics.device_restarts << '|' << error_code_ << '|'
        << metrics.native_xruns << '|' << metrics.ring_overflow_frames << '|'
        << metrics.timestamp_gap_frames << '|' << metrics.writer_stalls << '|'
        << metrics.route_changes << '|' << metrics.peak_level << '|'
        << metrics.rms_level << '|' << metrics.hardware_timestamps << '|'
        << metrics.timestamp_query_failures;
  return value.str();
}

oboe::DataCallbackResult AndroidOboeCapture::onAudioReady(
    oboe::AudioStream* /*stream*/, void* audio_data, int32_t num_frames) {
  if (audio_data == nullptr || num_frames <= 0 || ring_ == nullptr) {
    return oboe::DataCallbackResult::Stop;
  }
  if (paused_.load(std::memory_order_acquire)) {
    return oboe::DataCallbackResult::Continue;
  }
  const auto byte_count = static_cast<std::size_t>(num_frames) *
                          format_.bytes_per_frame();
  const auto input = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(audio_data), byte_count);
  const auto stored = ring_->push(input);
  session_.count_captured_frames(stored / format_.bytes_per_frame());
  if (stored < byte_count) {
    session_.count_dropped_frames((byte_count - stored) /
                                  format_.bytes_per_frame());
  }
  return oboe::DataCallbackResult::Continue;
}

void AndroidOboeCapture::onErrorAfterClose(
    oboe::AudioStream* /*stream*/, oboe::Result error) {
  if (stop_requested_.load(std::memory_order_acquire)) {
    return;
  }
  if (error != oboe::Result::ErrorDisconnected) {
    SetError("oboe_stream_failed");
    session_.fail();
    capture_finished_.store(true, std::memory_order_release);
    return;
  }
  SetError("device_disconnected");
  session_.count_discontinuity();
  recovery_coordinator_.SetRoute(-1, false);
  {
    std::lock_guard lock(mutex_);
    stream_.reset();
  }
  if (recovery_thread_.joinable()) {
    recovery_thread_.join();
  }
  recovery_thread_ = std::thread(&AndroidOboeCapture::RecoveryWorker, this);
}

void AndroidOboeCapture::RecoveryWorker() {
  if (stop_requested_.load(std::memory_order_acquire) ||
      !SubmitStorageCommand(3)) {
    return;
  }
  const auto deadline = AndroidRecoveryCoordinator::Clock::now() +
                        AndroidRecoveryCoordinator::kRecoveryDeadline;
  auto recovery = recovery_coordinator_.Current();
  while (true) {
    if (stop_requested_.load(std::memory_order_acquire)) {
      return;
    }
    const auto decision = AndroidRecoveryCoordinator::Decide(
        recovery, AndroidRecoveryCoordinator::Clock::now(), deadline);
    if (decision == AndroidRecoveryCoordinator::Decision::stop) {
      return;
    }
    if (decision == AndroidRecoveryCoordinator::Decision::exhausted) {
      break;
    }
    if (decision == AndroidRecoveryCoordinator::Decision::attempt &&
        OpenReplacementStream()) {
      lifecycle_generation_.fetch_add(1, std::memory_order_relaxed);
      session_.count_device_restart();
      session_.count_route_change();
      SetError({});
      static_cast<void>(SubmitStorageCommand(4));
      return;
    }
    const auto wake_at = std::min(
        AndroidRecoveryCoordinator::Clock::now() +
            AndroidRecoveryCoordinator::kRetryInterval,
        deadline);
    recovery = recovery_coordinator_.WaitUntil(wake_at, recovery.generation);
  }
  SetError("device_reconnect_exhausted");
  session_.fail();
  capture_finished_.store(true, std::memory_order_release);
}

bool AndroidOboeCapture::OpenReplacementStream() {
  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Input)
      ->setSharingMode(oboe::SharingMode::Shared)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setFormat(oboe::AudioFormat::I16)
      ->setFormatConversionAllowed(true)
      ->setInputPreset(oboe::InputPreset::Unprocessed)
      ->setDataCallback(this)
      ->setErrorCallback(this);
  const auto input_device_id =
      input_device_id_.load(std::memory_order_relaxed);
  if (input_device_id >= 0) {
    builder.setDeviceId(input_device_id);
  }
  std::shared_ptr<oboe::AudioStream> replacement;
  auto result = builder.openStream(replacement);
  if (result != oboe::Result::OK || replacement == nullptr) {
    builder.setInputPreset(oboe::InputPreset::Generic);
    result = builder.openStream(replacement);
  }
  if (result != oboe::Result::OK || replacement == nullptr ||
      (input_device_id >= 0 &&
       replacement->getDeviceId() != input_device_id) ||
      replacement->getFormat() != oboe::AudioFormat::I16 ||
      replacement->getSampleRate() != static_cast<int32_t>(format_.sample_rate) ||
      replacement->getChannelCount() != static_cast<int32_t>(format_.channel_count)) {
    if (replacement != nullptr) replacement->close();
    return false;
  }
  if (replacement->requestStart() != oboe::Result::OK) {
    replacement->close();
    return false;
  }
  std::lock_guard lock(mutex_);
  stream_ = std::move(replacement);
  return true;
}

void AndroidOboeCapture::StorageWorker(
    std::filesystem::path session_directory) {
  try {
    shipglows::audio::SegmentedWavStore store(
        std::move(session_directory), format_,
        static_cast<std::uint64_t>(format_.sample_rate) * 5);
    const auto frame_bytes = format_.bytes_per_frame();
    const auto chunk_bytes = (64 * 1024 / frame_bytes) * frame_bytes;
    std::vector<std::byte> chunk(chunk_bytes);
    auto next_timestamp_query = std::chrono::steady_clock::now();
    while (!capture_finished_.load(std::memory_order_acquire) ||
           (ring_ != nullptr && ring_->available_to_read() > 0)) {
      if (std::chrono::steady_clock::now() >= next_timestamp_query) {
        next_timestamp_query = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(100);
        std::shared_ptr<oboe::AudioStream> timestamp_stream;
        {
          std::lock_guard lock(mutex_);
          timestamp_stream = stream_;
        }
        if (timestamp_stream != nullptr) {
          const auto timestamp = timestamp_stream->getTimestamp(CLOCK_MONOTONIC);
          if (timestamp) {
            const auto value = timestamp.value();
            const auto gaps = timestamp_tracker_.observe_position(
                static_cast<std::uint64_t>(value.timestamp),
                static_cast<std::uint64_t>(value.position),
                lifecycle_generation_.load(std::memory_order_relaxed));
            session_.count_hardware_timestamp();
            if (gaps > 0) session_.count_timestamp_gap_frames(gaps);
          } else {
            session_.count_timestamp_query_failure();
          }
        }
      }
      const auto available = ring_ == nullptr ? 0 : ring_->pop(chunk);
      if (available == 0) {
        const auto command = storage_command_.exchange(0);
        if (command == 1) {
          store.checkpoint(shipglows::audio::SessionEvent::pause, "user_pause");
        } else if (command == 2) {
          store.checkpoint(shipglows::audio::SessionEvent::resume, "user_resume");
        } else if (command == 3) {
          store.checkpoint(shipglows::audio::SessionEvent::interruption,
                           "device_disconnected");
        } else if (command == 4) {
          store.checkpoint(shipglows::audio::SessionEvent::device_restart,
                           "selected_input_device");
        } else if (command == 5) {
          store.checkpoint(shipglows::audio::SessionEvent::route_change,
                           "selected_input_device");
        }
        if (command != 0) {
          {
            std::lock_guard lock(mutex_);
            ++storage_command_completed_;
          }
          storage_command_condition_.notify_all();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      const auto block = std::span<const std::byte>(chunk.data(), available);
      session_.count_clipped_samples(
          shipglows::audio::count_clipped_samples(block, format_));
      const auto levels = shipglows::audio::analyze_levels(block, format_);
      session_.set_levels(levels.peak, levels.rms);
      store.append(block);
    }
    store.finalize();
    if (session_.state() == shipglows::audio::SessionState::stopping) {
      static_cast<void>(session_.finish());
    }
  } catch (...) {
    SetError("storage_write_failed");
    session_.fail();
    capture_finished_.store(true, std::memory_order_release);
  }
}

bool AndroidOboeCapture::SubmitStorageCommand(std::uint8_t command) {
  std::unique_lock lock(mutex_);
  const auto expected_completion = storage_command_completed_ + 1;
  storage_command_.store(command, std::memory_order_release);
  return storage_command_condition_.wait_for(
      lock, std::chrono::seconds(5), [this, expected_completion] {
        return storage_command_completed_ >= expected_completion ||
               stop_requested_.load(std::memory_order_acquire) ||
               session_.state() == shipglows::audio::SessionState::failed;
      }) && storage_command_completed_ >= expected_completion;
}

void AndroidOboeCapture::SetError(std::string error_code) {
  std::lock_guard lock(mutex_);
  error_code_ = std::move(error_code);
}

}  // namespace shipglows_audio
