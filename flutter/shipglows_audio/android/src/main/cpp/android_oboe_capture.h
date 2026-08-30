#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "shipglows/audio/audio_types.hpp"
#include "shipglows/audio/audio_lifecycle.hpp"
#include "shipglows/audio/capture_session.hpp"
#include "shipglows/audio/spsc_audio_ring_buffer.hpp"

namespace shipglows_audio {

class AndroidOboeCapture final : public oboe::AudioStreamDataCallback,
                                 public oboe::AudioStreamErrorCallback {
 public:
  AndroidOboeCapture();
  ~AndroidOboeCapture() override;

  AndroidOboeCapture(const AndroidOboeCapture&) = delete;
  AndroidOboeCapture& operator=(const AndroidOboeCapture&) = delete;

  [[nodiscard]] bool Start(const std::filesystem::path& session_directory);
  void Stop();
  [[nodiscard]] std::string StatusLine() const;

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream,
                                        void* audio_data,
                                        int32_t num_frames) override;
  void onErrorAfterClose(oboe::AudioStream* stream,
                         oboe::Result error) override;

 private:
  void StorageWorker(std::filesystem::path session_directory);
  void SetError(std::string error_code);

  mutable std::mutex mutex_;
  std::shared_ptr<oboe::AudioStream> stream_;
  shipglows::audio::CaptureSession session_;
  shipglows::audio::AudioFormat format_{};
  std::string error_code_;
  std::unique_ptr<shipglows::audio::SpscAudioRingBuffer<std::byte>> ring_;
  std::thread storage_thread_;
  std::atomic<bool> capture_finished_{false};
  shipglows::audio::TimestampTracker timestamp_tracker_;
  std::atomic<std::uint64_t> lifecycle_generation_{1};
};

}  // namespace shipglows_audio
