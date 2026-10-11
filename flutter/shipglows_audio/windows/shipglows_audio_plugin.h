#ifndef FLUTTER_PLUGIN_SHIPGLOWS_AUDIO_PLUGIN_H_
#define FLUTTER_PLUGIN_SHIPGLOWS_AUDIO_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>
#include <optional>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "shipglows/audio/analysis_stream.hpp"

namespace shipglows_audio {

static_assert(std::atomic<shipglows::audio::AnalysisStream*>::is_always_lock_free,
              "Windows analysis callback requires lock-free stream publication");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "Windows analysis callback requires lock-free reader fencing");

class WindowsWasapiCapture;
class WindowsWasapiPlayback;
class WindowsLocalPlayback;

class ShipglowsAudioPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  explicit ShipglowsAudioPlugin(
      flutter::PluginRegistrarWindows* registrar = nullptr);

  virtual ~ShipglowsAudioPlugin();

  // Disallow copy and assign.
  ShipglowsAudioPlugin(const ShipglowsAudioPlugin&) = delete;
  ShipglowsAudioPlugin& operator=(const ShipglowsAudioPlugin&) = delete;

  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

 private:
  void ClearAnalysisStream(bool clean_eof);
  std::optional<LRESULT> HandleWindowProc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);

  flutter::PluginRegistrarWindows* registrar_ = nullptr;
  int window_proc_delegate_id_ = -1;
  std::unique_ptr<WindowsWasapiCapture> capture_;
  std::unique_ptr<WindowsLocalPlayback> playback_;
  std::unique_ptr<WindowsWasapiPlayback> playback_output_;
  // Kept alive until playback_output_ has quiesced its callback on session reload.
  std::unique_ptr<shipglows::audio::AnalysisStream> analysis_stream_;
  std::atomic<shipglows::audio::AnalysisStream*> analysis_stream_tap_{nullptr};
  std::atomic<std::uint32_t> analysis_callbacks_active_{0};
  std::string analysis_lease_id_;
  std::string analysis_stream_id_;
  std::string analysis_source_identity_;
  std::string analysis_profile_;
  std::string analysis_transform_digest_;
  unsigned analysis_feature_mask_ = 0;
  bool analysis_lease_stopped_ = false;
  std::int64_t playback_generation_ = 0;
  double playback_speed_ = 1.0;
  std::string playback_state_ = "stopped";
  std::vector<std::wstring> input_endpoint_ids_;
#ifdef SHIPGLOWS_AUDIO_A0
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> diagnostic_channel_;
#endif
};

}  // namespace shipglows_audio

#endif  // FLUTTER_PLUGIN_SHIPGLOWS_AUDIO_PLUGIN_H_
