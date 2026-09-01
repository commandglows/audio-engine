#include "shipglows_audio_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <map>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "shipglows/audio/engine_info.hpp"
#include "windows_wasapi_capture.h"

namespace shipglows_audio {
namespace {

using Microsoft::WRL::ComPtr;

std::string SampleFormatName(shipglows::audio::SampleFormat format) {
  switch (format) {
    case shipglows::audio::SampleFormat::int16:
      return "int16";
    case shipglows::audio::SampleFormat::int24:
      return "int24";
    case shipglows::audio::SampleFormat::int32:
      return "int32";
    case shipglows::audio::SampleFormat::float32:
      return "float32";
  }
  return "unknown";
}

flutter::EncodableValue CaptureStatusValue(const WasapiCaptureStatus& status) {
  flutter::EncodableMap value;
  value[flutter::EncodableValue("state")] = flutter::EncodableValue(
      std::string(shipglows::audio::to_string(status.state)));
  value[flutter::EncodableValue("sampleRate")] = flutter::EncodableValue(
      static_cast<int>(status.format.sample_rate));
  value[flutter::EncodableValue("channelCount")] = flutter::EncodableValue(
      static_cast<int>(status.format.channel_count));
  value[flutter::EncodableValue("sampleFormat")] =
      flutter::EncodableValue(SampleFormatName(status.format.sample_format));
  value[flutter::EncodableValue("framesCaptured")] = flutter::EncodableValue(
      static_cast<int64_t>(status.metrics.frames_captured));
  value[flutter::EncodableValue("framesDropped")] = flutter::EncodableValue(
      static_cast<int64_t>(status.metrics.frames_dropped));
  value[flutter::EncodableValue("discontinuities")] =
      flutter::EncodableValue(
          static_cast<int64_t>(status.metrics.discontinuities));
  value[flutter::EncodableValue("clippedSamples")] = flutter::EncodableValue(
      static_cast<int64_t>(status.metrics.clipped_samples));
  value[flutter::EncodableValue("deviceRestarts")] = flutter::EncodableValue(
      static_cast<int64_t>(status.metrics.device_restarts));
  value[flutter::EncodableValue("nativeXruns")] = flutter::EncodableValue(
      static_cast<int64_t>(status.metrics.native_xruns));
  value[flutter::EncodableValue("ringOverflowFrames")] =
      flutter::EncodableValue(
          static_cast<int64_t>(status.metrics.ring_overflow_frames));
  value[flutter::EncodableValue("timestampGapFrames")] =
      flutter::EncodableValue(
          static_cast<int64_t>(status.metrics.timestamp_gap_frames));
  value[flutter::EncodableValue("writerStalls")] = flutter::EncodableValue(
      static_cast<int64_t>(status.metrics.writer_stalls));
  value[flutter::EncodableValue("routeChanges")] = flutter::EncodableValue(
      static_cast<int64_t>(status.metrics.route_changes));
  value[flutter::EncodableValue("hardwareTimestamps")] =
      flutter::EncodableValue(
          static_cast<int64_t>(status.metrics.hardware_timestamps));
  value[flutter::EncodableValue("timestampQueryFailures")] =
      flutter::EncodableValue(
          static_cast<int64_t>(status.metrics.timestamp_query_failures));
  value[flutter::EncodableValue("peakLevel")] =
      flutter::EncodableValue(static_cast<double>(status.metrics.peak_level));
  value[flutter::EncodableValue("rmsLevel")] =
      flutter::EncodableValue(static_cast<double>(status.metrics.rms_level));
  value[flutter::EncodableValue("errorCode")] =
      flutter::EncodableValue(status.error_code);
  const auto recoverable = status.error_code == "device_invalidated" ||
                           status.error_code == "route_changed" ||
                           status.error_code == "wasapi_event_wait_failed";
  value[flutter::EncodableValue("errorRecoverable")] =
      flutter::EncodableValue(recoverable);
  value[flutter::EncodableValue("recoveryAction")] = flutter::EncodableValue(
      recoverable ? "automatic_reconnect" :
      (status.error_code.empty() ? "none" : "start_new_session"));
  return flutter::EncodableValue(value);
}

const std::string* StringArgument(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    const char* key) {
  if (call.arguments() == nullptr) {
    return nullptr;
  }
  const auto* arguments = std::get_if<flutter::EncodableMap>(call.arguments());
  if (arguments == nullptr) {
    return nullptr;
  }
  const auto found = arguments->find(flutter::EncodableValue(key));
  return found == arguments->end()
             ? nullptr
             : std::get_if<std::string>(&found->second);
}

const int* IntArgument(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    const char* key) {
  if (call.arguments() == nullptr) return nullptr;
  const auto* arguments = std::get_if<flutter::EncodableMap>(call.arguments());
  if (arguments == nullptr) return nullptr;
  const auto found = arguments->find(flutter::EncodableValue(key));
  return found == arguments->end() ? nullptr
                                   : std::get_if<int>(&found->second);
}

std::string Utf8String(const std::wstring& value) {
  if (value.empty()) return {};
  const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (count <= 0) return {};
  std::string utf8(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), utf8.data(), count,
                      nullptr, nullptr);
  return utf8;
}

std::vector<std::pair<std::wstring, std::wstring>> EnumerateInputEndpoints() {
  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool should_uninitialize = SUCCEEDED(com_result);
  std::vector<std::pair<std::wstring, std::wstring>> endpoints;
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDeviceCollection> collection;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
      SUCCEEDED(enumerator->EnumAudioEndpoints(
          eCapture, DEVICE_STATE_ACTIVE, &collection))) {
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT index = 0; index < count; ++index) {
      ComPtr<IMMDevice> device;
      LPWSTR endpoint_id = nullptr;
      ComPtr<IPropertyStore> properties;
      PROPVARIANT name;
      PropVariantInit(&name);
      if (SUCCEEDED(collection->Item(index, &device)) &&
          SUCCEEDED(device->GetId(&endpoint_id)) && endpoint_id != nullptr &&
          SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
          SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
          name.vt == VT_LPWSTR && name.pwszVal != nullptr) {
        endpoints.emplace_back(endpoint_id, name.pwszVal);
      }
      if (endpoint_id != nullptr) CoTaskMemFree(endpoint_id);
      PropVariantClear(&name);
    }
  }
  if (should_uninitialize) CoUninitialize();
  return endpoints;
}

std::filesystem::path Utf8Path(const std::string& value) {
  if (value.empty()) {
    return {};
  }
  const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
  if (count <= 0) {
    return {};
  }
  std::wstring wide(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), wide.data(), count);
  return std::filesystem::path(wide);
}

}  // namespace

// static
void ShipglowsAudioPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "shipglows_audio",
          &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<ShipglowsAudioPlugin>(registrar);

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

ShipglowsAudioPlugin::ShipglowsAudioPlugin(
    flutter::PluginRegistrarWindows* registrar)
    : registrar_(registrar),
      capture_(std::make_unique<WindowsWasapiCapture>()) {
  if (registrar_ != nullptr) {
    window_proc_delegate_id_ = registrar_->RegisterTopLevelWindowProcDelegate(
        [this](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
          return HandleWindowProc(hwnd, message, wparam, lparam);
        });
  }
}

ShipglowsAudioPlugin::~ShipglowsAudioPlugin() {
  if (registrar_ != nullptr && window_proc_delegate_id_ >= 0) {
    registrar_->UnregisterTopLevelWindowProcDelegate(window_proc_delegate_id_);
  }
}

std::optional<LRESULT> ShipglowsAudioPlugin::HandleWindowProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message != WM_POWERBROADCAST) return std::nullopt;
  switch (ClassifyWasapiPowerBroadcast(wparam)) {
    case WasapiPowerEvent::suspend:
      capture_->NotifySystemSuspend();
      break;
    case WasapiPowerEvent::resume:
      capture_->NotifySystemResume();
      break;
    case WasapiPowerEvent::none:
      break;
  }
  return std::nullopt;
}

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
  } else if (method_call.method_name() == "startRecording") {
    const auto* directory = StringArgument(method_call, "sessionDirectory");
    const auto path = directory == nullptr ? std::filesystem::path{}
                                           : Utf8Path(*directory);
    if (path.empty()) {
      result->Error("invalid_session_directory",
                    "A valid UTF-8 session directory is required.");
      return;
    }
    if (NeedsFreshWasapiCapture(capture_->Status().state)) {
      capture_ = std::make_unique<WindowsWasapiCapture>();
    }
    const auto* input_device_id = IntArgument(method_call, "inputDeviceId");
    if (input_device_id != nullptr) {
      if (*input_device_id <= 0 ||
          static_cast<std::size_t>(*input_device_id) >
              input_endpoint_ids_.size()) {
        result->Error("input_device_not_found",
                      "The selected input device is no longer available.");
        return;
      }
      capture_->SelectEndpoint(
          input_endpoint_ids_[static_cast<std::size_t>(*input_device_id - 1)]);
    }
    if (!capture_->Start(path)) {
      const auto status = capture_->Status();
      result->Error(status.error_code.empty() ? "capture_start_failed"
                                              : status.error_code,
                    "The native WASAPI capture could not start.");
      return;
    }
    result->Success(CaptureStatusValue(capture_->Status()));
  } else if (method_call.method_name() == "getInputDevices") {
    const auto endpoints = EnumerateInputEndpoints();
    input_endpoint_ids_.clear();
    flutter::EncodableList devices;
    std::map<std::wstring, int> name_counts;
    std::map<std::wstring, int> name_ordinals;
    for (const auto& [endpoint_id, name] : endpoints) {
      static_cast<void>(endpoint_id);
      ++name_counts[name];
    }
    int id = 1;
    for (const auto& [endpoint_id, name] : endpoints) {
      input_endpoint_ids_.push_back(endpoint_id);
      auto display_name = name;
      if (name_counts[name] > 1) {
        display_name += L" [" + std::to_wstring(++name_ordinals[name]) + L"]";
      }
      flutter::EncodableMap device;
      device[flutter::EncodableValue("id")] = flutter::EncodableValue(id++);
      device[flutter::EncodableValue("name")] =
          flutter::EncodableValue(Utf8String(display_name));
      device[flutter::EncodableValue("type")] =
          flutter::EncodableValue("microphone");
      device[flutter::EncodableValue("isExternal")] =
          flutter::EncodableValue(false);
      devices.emplace_back(device);
    }
    result->Success(flutter::EncodableValue(devices));
  } else if (method_call.method_name() == "selectInputDevice") {
    const auto* input_device_id = IntArgument(method_call, "inputDeviceId");
    if (input_device_id == nullptr || *input_device_id <= 0 ||
        static_cast<std::size_t>(*input_device_id) >
            input_endpoint_ids_.size()) {
      result->Error("input_device_not_found",
                    "The selected input device is no longer available.");
      return;
    }
    capture_->SelectEndpoint(
        input_endpoint_ids_[static_cast<std::size_t>(*input_device_id - 1)]);
    result->Success(CaptureStatusValue(capture_->Status()));
  } else if (method_call.method_name() == "stopRecording") {
    result->Success(CaptureStatusValue(capture_->Stop()));
  } else if (method_call.method_name() == "pauseRecording") {
    result->Success(CaptureStatusValue(capture_->Pause()));
  } else if (method_call.method_name() == "resumeRecording") {
    result->Success(CaptureStatusValue(capture_->Resume()));
  } else if (method_call.method_name() == "getRecordingStatus") {
    result->Success(CaptureStatusValue(capture_->Status()));
  } else {
    result->NotImplemented();
  }
}

}  // namespace shipglows_audio
