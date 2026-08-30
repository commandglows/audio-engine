#include "android_oboe_capture.h"

#include <chrono>
#include <span>
#include <sstream>
#include <vector>

#include "shipglows/audio/pcm_analysis.hpp"
#include "shipglows/audio/segmented_wav_store.hpp"

namespace shipglows_audio {

AndroidOboeCapture::AndroidOboeCapture() = default;

AndroidOboeCapture::~AndroidOboeCapture() { Stop(); }

bool AndroidOboeCapture::Start(
    const std::filesystem::path& session_directory) {
  {
    std::lock_guard lock(mutex_);
    if (stream_ != nullptr ||
        session_.state() != shipglows::audio::SessionState::idle) {
      return false;
    }
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

  capture_finished_.store(false);
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
  std::shared_ptr<oboe::AudioStream> stream;
  {
    std::lock_guard lock(mutex_);
    stream = stream_;
    if (session_.state() == shipglows::audio::SessionState::recording) {
      static_cast<void>(session_.request_stop());
    }
  }
  if (stream != nullptr) {
    stream->requestStop();
    stream->close();
  }
  capture_finished_.store(true, std::memory_order_release);
  if (storage_thread_.joinable()) {
    storage_thread_.join();
  }
  std::lock_guard lock(mutex_);
  stream_.reset();
}

std::string AndroidOboeCapture::StatusLine() const {
  std::lock_guard lock(mutex_);
  const auto metrics = session_.metrics();
  std::ostringstream value;
  value << shipglows::audio::to_string(session_.state()) << '|'
        << format_.sample_rate << '|' << format_.channel_count << "|int16|"
        << metrics.frames_captured << '|' << metrics.frames_dropped << '|'
        << metrics.discontinuities << '|' << metrics.clipped_samples << '|'
        << metrics.device_restarts << '|' << error_code_;
  return value.str();
}

oboe::DataCallbackResult AndroidOboeCapture::onAudioReady(
    oboe::AudioStream* /*stream*/, void* audio_data, int32_t num_frames) {
  if (audio_data == nullptr || num_frames <= 0 || ring_ == nullptr) {
    return oboe::DataCallbackResult::Stop;
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
  SetError(error == oboe::Result::ErrorDisconnected
               ? "device_disconnected"
               : "oboe_stream_failed");
  session_.fail();
  capture_finished_.store(true, std::memory_order_release);
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
    while (!capture_finished_.load(std::memory_order_acquire) ||
           (ring_ != nullptr && ring_->available_to_read() > 0)) {
      const auto available = ring_ == nullptr ? 0 : ring_->pop(chunk);
      if (available == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      const auto block = std::span<const std::byte>(chunk.data(), available);
      session_.count_clipped_samples(
          shipglows::audio::count_clipped_samples(block, format_));
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

void AndroidOboeCapture::SetError(std::string error_code) {
  std::lock_guard lock(mutex_);
  error_code_ = std::move(error_code);
}

}  // namespace shipglows_audio
