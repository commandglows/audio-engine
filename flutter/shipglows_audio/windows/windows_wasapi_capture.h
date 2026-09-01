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

enum class WasapiRouteChange : std::uint8_t {
  none,
  selected_device_lost,
  default_device_changed,
};

enum class WasapiPowerEvent : std::uint8_t {
  none,
  suspend,
  resume,
};

[[nodiscard]] WasapiPowerEvent ClassifyWasapiPowerBroadcast(
    std::uintptr_t event) noexcept;

[[nodiscard]] constexpr bool ShouldQueueWasapiResume(
    bool system_suspended, bool suspend_requested,
    bool resume_in_progress) noexcept {
  return (system_suspended || suspend_requested) && !resume_in_progress;
}

[[nodiscard]] constexpr WasapiRouteChange ClassifyWasapiRouteChange(
    bool default_device_changed, bool selected_device_active) noexcept {
  if (!default_device_changed) return WasapiRouteChange::none;
  return selected_device_active ? WasapiRouteChange::default_device_changed
                                : WasapiRouteChange::selected_device_lost;
}

[[nodiscard]] constexpr bool NeedsFreshWasapiCapture(
    shipglows::audio::SessionState state) noexcept {
  return state == shipglows::audio::SessionState::stopped ||
         state == shipglows::audio::SessionState::failed;
}

class WindowsWasapiCapture final {
 public:
  WindowsWasapiCapture();
  ~WindowsWasapiCapture();

  WindowsWasapiCapture(const WindowsWasapiCapture&) = delete;
  WindowsWasapiCapture& operator=(const WindowsWasapiCapture&) = delete;

  [[nodiscard]] bool Start(const std::filesystem::path& session_directory);
  void SelectEndpoint(std::wstring endpoint_id);
  [[nodiscard]] WasapiCaptureStatus Stop();
  [[nodiscard]] WasapiCaptureStatus Pause();
  [[nodiscard]] WasapiCaptureStatus Resume();
  [[nodiscard]] WasapiCaptureStatus Status() const;
  void NotifySystemSuspend();
  void NotifySystemResume();

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
  std::mutex endpoint_mutex_;
  std::wstring selected_endpoint_id_;
  bool follows_system_default_ = true;
  std::atomic<WasapiRouteChange> route_change_requested_{
      WasapiRouteChange::none};
  std::atomic<bool> suspend_requested_{false};
  std::atomic<bool> resume_requested_{false};
  std::atomic<bool> system_suspended_{false};
  std::atomic<bool> resume_to_user_pause_{false};
  std::atomic<bool> resume_in_progress_{false};

  [[nodiscard]] bool SubmitStorageCommand(std::uint8_t command);
};

}  // namespace shipglows_audio

#endif  // FLUTTER_PLUGIN_WINDOWS_WASAPI_CAPTURE_H_
