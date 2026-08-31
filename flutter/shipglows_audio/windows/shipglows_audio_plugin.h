#ifndef FLUTTER_PLUGIN_SHIPGLOWS_AUDIO_PLUGIN_H_
#define FLUTTER_PLUGIN_SHIPGLOWS_AUDIO_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>
#include <optional>

namespace shipglows_audio {

class WindowsWasapiCapture;

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
  std::optional<LRESULT> HandleWindowProc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);

  flutter::PluginRegistrarWindows* registrar_ = nullptr;
  int window_proc_delegate_id_ = -1;
  std::unique_ptr<WindowsWasapiCapture> capture_;
};

}  // namespace shipglows_audio

#endif  // FLUTTER_PLUGIN_SHIPGLOWS_AUDIO_PLUGIN_H_
