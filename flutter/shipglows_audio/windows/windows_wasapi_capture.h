#ifndef FLUTTER_PLUGIN_WINDOWS_WASAPI_CAPTURE_H_
#define FLUTTER_PLUGIN_WINDOWS_WASAPI_CAPTURE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
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

struct WasapiCaptureStatus final {
  shipglows::audio::SessionState state =
      shipglows::audio::SessionState::idle;
  shipglows::audio::AudioFormat format{};
  shipglows::audio::CaptureMetrics metrics{};
  std::string error_code;
};

class WindowsWasapiCapture final {
 public:
  WindowsWasapiCapture();
  ~WindowsWasapiCapture();

  WindowsWasapiCapture(const WindowsWasapiCapture&) = delete;
  WindowsWasapiCapture& operator=(const WindowsWasapiCapture&) = delete;

  [[nodiscard]] bool Start(const std::filesystem::path& session_directory);
  [[nodiscard]] WasapiCaptureStatus Stop();
  [[nodiscard]] WasapiCaptureStatus Pause();
  [[nodiscard]] WasapiCaptureStatus Resume();
  [[nodiscard]] WasapiCaptureStatus Status() const;

 private:
  void CaptureWorker(std::filesystem::path session_directory);
  void StorageWorker(std::filesystem::path session_directory);
  void RouteMonitorWorker();
  void SetInitializationResult(bool success, std::string error_code = {});
  void SetError(std::string error_code);

  mutable std::mutex state_mutex_;
  std::condition_variable initialization_condition_;
  std::condition_variable storage_command_condition_;
  bool initialization_finished_ = false;
  bool initialization_succeeded_ = false;
  std::string error_code_;
  shipglows::audio::AudioFormat format_{};

  shipglows::audio::CaptureSession session_;
  std::unique_ptr<shipglows::audio::SpscAudioRingBuffer<std::byte>> ring_;
  std::thread capture_thread_;
  std::thread storage_thread_;
  std::thread route_monitor_thread_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> capture_finished_{false};
  std::atomic<bool> paused_{false};
  std::atomic<std::uint8_t> storage_command_{0};
  std::uint64_t storage_command_completed_ = 0;
  std::atomic<void*> wake_event_{nullptr};
  shipglows::audio::TimestampTracker timestamp_tracker_;
  std::atomic<std::uint64_t> lifecycle_generation_{1};
  std::atomic<bool> route_change_requested_{false};

  [[nodiscard]] bool SubmitStorageCommand(std::uint8_t command);
};

}  // namespace shipglows_audio

#endif  // FLUTTER_PLUGIN_WINDOWS_WASAPI_CAPTURE_H_
