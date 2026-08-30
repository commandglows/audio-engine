#include "shipglows_audio_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>

#include "shipglows/audio/engine_info.hpp"

namespace shipglows_audio {

// static
void ShipglowsAudioPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "shipglows_audio",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<ShipglowsAudioPlugin>();

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

ShipglowsAudioPlugin::ShipglowsAudioPlugin() {}

ShipglowsAudioPlugin::~ShipglowsAudioPlugin() {}

void ShipglowsAudioPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name() == "getEngineInfo") {
    flutter::EncodableMap info;
    info[flutter::EncodableValue("name")] = flutter::EncodableValue(
        std::string(shipglows::audio::kEngineName));
    info[flutter::EncodableValue("version")] = flutter::EncodableValue(
        std::string(shipglows::audio::kEngineVersion));
    info[flutter::EncodableValue("platform")] = flutter::EncodableValue("windows");
    info[flutter::EncodableValue("backend")] = flutter::EncodableValue("wasapi-pending");
    info[flutter::EncodableValue("nativeCoreLoaded")] =
        flutter::EncodableValue(true);
    result->Success(flutter::EncodableValue(info));
  } else {
    result->NotImplemented();
  }
}

}  // namespace shipglows_audio
