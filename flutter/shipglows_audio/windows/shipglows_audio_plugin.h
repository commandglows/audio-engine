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
class WindowsLocalPlayback;
class WindowsOutputBus;
class BusPlaybackOutput;

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
  enum class AnalysisSource { none, playback_output, output_mix };

  void ClearAnalysisStream(bool clean_eof);
  // Ends a playback-fenced lease and forgets its identity; mix leases survive.
  void ReleasePlaybackAnalysisLease(bool clean_eof);
  void HandleSynthCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);
  std::optional<LRESULT> HandleWindowProc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);

  flutter::PluginRegistrarWindows* registrar_ = nullptr;
  int window_proc_delegate_id_ = -1;
  std::unique_ptr<WindowsWasapiCapture> capture_;
  // One process-wide render stream mixing playback and the synthesizer.
  std::unique_ptr<WindowsOutputBus> output_bus_;
  std::unique_ptr<WindowsLocalPlayback> playback_;
  std::unique_ptr<BusPlaybackOutput> playback_output_;
  // Freed only after the bus has fenced its analysis tap.
  std::unique_ptr<shipglows::audio::AnalysisStream> analysis_stream_;
  AnalysisSource analysis_source_ = AnalysisSource::none;
  std::int64_t analysis_generation_ = 0;
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
  std::int64_t synth_generation_ = 0;
  bool synth_active_ = false;
  std::vector<std::wstring> input_endpoint_ids_;
#ifdef SHIPGLOWS_AUDIO_A0
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> diagnostic_channel_;
#endif
};

}  // namespace shipglows_audio

#endif  // FLUTTER_PLUGIN_SHIPGLOWS_AUDIO_PLUGIN_H_
