#ifndef SHIPGLOWS_AUDIO_WINDOWS_WASAPI_PLAYBACK_H_
#define SHIPGLOWS_AUDIO_WINDOWS_WASAPI_PLAYBACK_H_

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace shipglows_audio {

enum class WasapiPlaybackFailure : std::uint8_t {
  none,
  already_running,
  invalid_endpoint,
  device_unavailable,
  unsupported_format,
  initialization_failed,
  start_failed,
  stream_failed,
  control_failed,
};

struct WasapiPlaybackResult final {
  WasapiPlaybackFailure failure = WasapiPlaybackFailure::none;
  long hresult = 0;
  explicit operator bool() const noexcept {
    return failure == WasapiPlaybackFailure::none;
  }
};

struct WasapiPlaybackMetrics final {
  std::uint64_t callbacks = 0;
  std::uint64_t underruns = 0;
  std::uint64_t last_callback_microseconds = 0;
  std::uint64_t max_callback_microseconds = 0;
  std::uint64_t p99_callback_microseconds = 0;
  std::uint32_t sample_rate = 0;
  std::uint32_t channels = 0;
  std::uint32_t buffer_frames = 0;
};

// Fills `frames` interleaved float frames and returns true on success. False
// asks the adapter to submit silence and count an underrun. Called only by the
// WASAPI event thread; bounded, allocation-free, non-blocking, and noexcept.
using WasapiRenderFunction = bool (*)(float* interleaved, std::uint32_t frames,
                                      std::uint32_t channels,
                                      void* context) noexcept;

class WindowsWasapiPlayback final {
 public:
  WindowsWasapiPlayback(WasapiRenderFunction render, void* context);
  ~WindowsWasapiPlayback();
  WindowsWasapiPlayback(const WindowsWasapiPlayback&) = delete;
  WindowsWasapiPlayback& operator=(const WindowsWasapiPlayback&) = delete;

  // Empty endpoint_id follows the current system default; otherwise the
  // endpoint ID is opened explicitly. Start waits for device initialization.
  WasapiPlaybackResult Start(const std::wstring& endpoint_id = {});
  WasapiPlaybackResult Pause();
  WasapiPlaybackResult Resume();
  WasapiPlaybackResult Stop();
  [[nodiscard]] WasapiPlaybackMetrics Metrics() const noexcept;

 private:
  enum class Command : std::uint8_t { none, start, pause, resume, stop };
  void Worker();

  WasapiRenderFunction render_;
  void* context_;
  mutable std::mutex mutex_;
  std::condition_variable command_cv_;
  std::condition_variable completion_cv_;
  std::thread worker_;
  Command command_ = Command::none;
  std::wstring endpoint_id_;
  WasapiPlaybackResult result_{};
  std::uint64_t request_id_ = 0;
  std::uint64_t completed_id_ = 0;
  bool shutting_down_ = false;
  bool worker_exited_ = false;
  std::atomic<std::uint64_t> callbacks_{0};
  std::atomic<std::uint64_t> underruns_{0};
  std::atomic<std::uint64_t> last_callback_us_{0};
  std::atomic<std::uint64_t> max_callback_us_{0};
  std::array<std::atomic<std::uint64_t>, 64> callback_time_buckets_{};
  std::atomic<std::uint32_t> sample_rate_{0};
  std::atomic<std::uint32_t> channels_{0};
  std::atomic<std::uint32_t> buffer_frames_{0};
  std::atomic<void*> control_event_{nullptr};
};

}  // namespace shipglows_audio

#endif  // SHIPGLOWS_AUDIO_WINDOWS_WASAPI_PLAYBACK_H_
