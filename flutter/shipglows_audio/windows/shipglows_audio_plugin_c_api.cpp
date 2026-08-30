#include "include/shipglows_audio/shipglows_audio_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "shipglows_audio_plugin.h"

void ShipglowsAudioPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  shipglows_audio::ShipglowsAudioPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
