#ifndef NOMINMAX
#define NOMINMAX
#endif

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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iterator>
#include <memory>
#include <map>
#include <mutex>
#include <limits>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "shipglows/audio/engine_info.hpp"
#include "shipglows/audio/dsp_chain.hpp"
#include "windows_wasapi_capture.h"
#include "windows_mf_decoder.h"
#include "windows_wasapi_playback.h"

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
  if (status.output_active_milliseconds) {
    value[flutter::EncodableValue("outputActiveMilliseconds")] =
        flutter::EncodableValue(static_cast<int64_t>(*status.output_active_milliseconds));
    value[flutter::EncodableValue("outputSilentMilliseconds")] =
        flutter::EncodableValue(static_cast<int64_t>(*status.output_silent_milliseconds));
  }
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

std::vector<std::pair<std::wstring, std::wstring>> EnumerateInputEndpoints(EDataFlow flow = eCapture) {
  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool should_uninitialize = SUCCEEDED(com_result);
  std::vector<std::pair<std::wstring, std::wstring>> endpoints;
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDeviceCollection> collection;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
      SUCCEEDED(enumerator->EnumAudioEndpoints(
          flow, DEVICE_STATE_ACTIVE, &collection))) {
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
    // First-run UI selection uses the real multimedia default endpoint. This
    // changes display order only; a started take still pins its exact ID.
    ComPtr<IMMDevice> default_device;
    LPWSTR default_id = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(
            flow, eMultimedia, &default_device)) &&
        SUCCEEDED(default_device->GetId(&default_id)) && default_id != nullptr) {
      const auto selected = std::find_if(endpoints.begin(), endpoints.end(),
          [default_id](const auto& endpoint) { return endpoint.first == default_id; });
      if (selected != endpoints.end()) {
        std::rotate(endpoints.begin(), selected, std::next(selected));
      }
    }
    if (default_id != nullptr) CoTaskMemFree(default_id);
  }
  collection.Reset();
  enumerator.Reset();
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

// The decoder is confined to this producer thread. WASAPI consumes a bounded
// SPSC ring and never calls Flutter or allocates; audio data never crosses the
// method channel.
class WindowsLocalPlayback final {
 public:
  static constexpr std::uint64_t kCapacityFrames = 48'000 * 8;
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "playback ring indices must be lock-free on the Windows target");

  WindowsLocalPlayback() : producer_(&WindowsLocalPlayback::Produce, this) {}
  ~WindowsLocalPlayback() {
    {
      std::lock_guard<std::mutex> lock(command_mutex_);
      shutting_down_ = true;
    }
    command_cv_.notify_one();
    if (producer_.joinable()) producer_.join();
  }
  WindowsLocalPlayback(const WindowsLocalPlayback&) = delete;
  WindowsLocalPlayback& operator=(const WindowsLocalPlayback&) = delete;

  bool Open(const std::filesystem::path& path, double seek) {
    std::unique_lock<std::mutex> lock(command_mutex_);
    path_ = path;
    seek_ = seek;
    command_ = Command::open;
    const auto request = ++request_id_;
    command_cv_.notify_one();
    command_done_.wait(lock, [&] { return completed_id_ >= request || shutting_down_ || worker_failed_; });
    return open_ok_;
  }
  bool Seek(double seconds) {
    std::unique_lock<std::mutex> lock(command_mutex_);
    seek_ = seconds;
    command_ = Command::seek;
    const auto request = ++request_id_;
    command_cv_.notify_one();
    command_done_.wait(lock, [&] { return completed_id_ >= request || shutting_down_ || worker_failed_; });
    return open_ok_;
  }
  double duration() const noexcept { return duration_.load(std::memory_order_relaxed); }
  double position() const noexcept {
    const double value = position_seconds_.load(std::memory_order_relaxed);
    const double length = duration_.load(std::memory_order_relaxed);
    return length > 0 ? std::min(value, length) : value;
  }
  bool completed() const noexcept { return drained_eof_.load(std::memory_order_acquire); }
  bool failed() const noexcept { return drained_error_.load(std::memory_order_acquire); }
  void SetSpeed(double value) noexcept { speed_.store(value, std::memory_order_relaxed); }
  bool SetEffects(const shipglows::audio::DspParameters& value) noexcept {
    return dsp_.set_parameters(value);
  }

  static bool Render(float* output, std::uint32_t frames, std::uint32_t channels,
                     void* context) noexcept {
    auto* self = static_cast<WindowsLocalPlayback*>(context);
    if (!self || !output || channels != 2) return false;
    std::fill(output, output + static_cast<std::size_t>(frames) * 2, 0.0F);
    if (frames == 0) return true;
    auto read = self->read_frames_.load(std::memory_order_relaxed);
    const auto write = self->write_frames_.load(std::memory_order_acquire);
    double phase = self->phase_;
    const double speed = self->speed_.load(std::memory_order_relaxed);
    const auto required = static_cast<std::uint64_t>(
        std::floor(phase + speed * static_cast<double>(frames - 1))) + 2;
    const bool eof = self->source_eof_.load(std::memory_order_acquire);
    if (write - read < required && !eof) {
      // Recheck EOF to avoid reporting starvation if the producer published
      // clean EOF while the ring indices were being sampled.
      if (!self->source_eof_.load(std::memory_order_acquire)) {
        if (self->decode_failed_.load(std::memory_order_acquire)) {
          self->drained_error_.store(true, std::memory_order_release);
        }
        return false;
      }
    }
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
      const auto whole = static_cast<std::uint64_t>(phase);
      const auto source_frame = read + whole;
      if (source_frame >= write) break;
      const auto a = source_frame % kCapacityFrames;
      const auto b = (source_frame + 1 < write ? source_frame + 1 : source_frame) % kCapacityFrames;
      const float fraction = static_cast<float>(phase - static_cast<double>(whole));
      for (std::size_t channel = 0; channel < 2; ++channel) {
        const float first = self->ring_[static_cast<std::size_t>(a) * 2 + channel];
        const float second = self->ring_[static_cast<std::size_t>(b) * 2 + channel];
        output[static_cast<std::size_t>(frame) * 2 + channel] =
            first + (second - first) * fraction;
      }
      phase += speed;
      const auto advance = static_cast<std::uint64_t>(phase);
      read = std::min(read + advance, write);
      phase -= static_cast<double>(advance);
      if (read == write) phase = 0.0;
    }
    self->phase_ = phase;
    self->read_frames_.store(read, std::memory_order_release);
    self->position_seconds_.store(self->source_offset_.load(std::memory_order_relaxed) +
        (static_cast<double>(read) + phase) / 48'000.0, std::memory_order_relaxed);
    if (read >= write) {
      if (self->decode_failed_.load(std::memory_order_acquire)) {
        self->drained_error_.store(true, std::memory_order_release);
      } else if (self->source_eof_.load(std::memory_order_acquire)) {
        self->drained_eof_.store(true, std::memory_order_release);
      }
    }
    // A clean EOF leaves the pre-zeroed output block intact. Continue passing
    // those zero frames through DSP so the fixed limiter lookahead drains its
    // final source samples instead of truncating them when the decoder stops.
    self->dsp_.process(output, frames);
    return true;
  }

 private:
  enum class Command { none, open, seek };
  void Produce() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) {
      std::lock_guard<std::mutex> lock(command_mutex_);
      open_ok_ = false;
      worker_failed_ = true;
      command_done_.notify_all();
      return;
    }
    WindowsMfDecoder decoder;
    std::vector<float> decoded;
    decoded.reserve(4096);
    bool opened = false;
    bool eof = false;
    for (;;) {
      Command command = Command::none;
      std::filesystem::path path;
      double seek = 0;
      std::uint64_t request = 0;
      {
        std::lock_guard<std::mutex> lock(command_mutex_);
        if (shutting_down_) break;
        command = command_;
        if (command != Command::none) {
          path = path_;
          seek = seek_;
          request = request_id_;
          command_ = Command::none;
        }
      }
      if (command != Command::none) {
        bool success = false;
        if (command == Command::open) {
          const auto result = decoder.Open(path);
          success = static_cast<bool>(result);
          if (success) {
            duration_.store(decoder.duration_seconds(), std::memory_order_relaxed);
            success = static_cast<bool>(decoder.Seek(seek));
          }
          opened = success;
        } else if (opened) {
          success = static_cast<bool>(decoder.Seek(seek));
        }
        if (success) {
          read_frames_.store(0, std::memory_order_relaxed);
          write_frames_.store(0, std::memory_order_relaxed);
          phase_ = 0;
          source_offset_.store(seek, std::memory_order_relaxed);
          position_seconds_.store(seek, std::memory_order_relaxed);
          source_eof_.store(false, std::memory_order_relaxed);
          decode_failed_.store(false, std::memory_order_relaxed);
          drained_eof_.store(false, std::memory_order_relaxed);
          drained_error_.store(false, std::memory_order_relaxed);
          eof = false;
        }
        {
          std::lock_guard<std::mutex> lock(command_mutex_);
          open_ok_ = success;
          completed_id_ = request;
        }
        command_done_.notify_all();
        continue;
      }
      if (!opened || eof) {
        std::unique_lock<std::mutex> lock(command_mutex_);
        command_cv_.wait_for(lock, std::chrono::milliseconds(10), [&] {
          return shutting_down_ || command_ != Command::none;
        });
        continue;
      }
      const auto read = read_frames_.load(std::memory_order_acquire);
      const auto write = write_frames_.load(std::memory_order_relaxed);
      if (write - read + 1024 > kCapacityFrames) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      const auto decoded_result = decoder.ReadFrames(1024, decoded);
      if (!decoded_result) {
        decode_failed_.store(true, std::memory_order_release);
        eof = true;
        continue;
      }
      const auto frames = decoded.size() / 2;
      if (frames == 0) {
        source_eof_.store(true, std::memory_order_release);
        eof = true;
        continue;
      }
      for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto slot = (write + frame) % kCapacityFrames;
        ring_[static_cast<std::size_t>(slot) * 2] = decoded[frame * 2];
        ring_[static_cast<std::size_t>(slot) * 2 + 1] = decoded[frame * 2 + 1];
      }
      write_frames_.store(write + frames, std::memory_order_release);
    }
    decoder = WindowsMfDecoder{};
    CoUninitialize();
  }

  std::array<float, static_cast<std::size_t>(kCapacityFrames * 2)> ring_{};
  std::atomic<std::uint64_t> read_frames_{0}, write_frames_{0};
  std::atomic<double> speed_{1.0}, duration_{0.0}, source_offset_{0.0}, position_seconds_{0.0};
  std::atomic<bool> source_eof_{false}, decode_failed_{false};
  std::atomic<bool> drained_eof_{false}, drained_error_{false};
  double phase_ = 0.0;  // audio callback thread only
  shipglows::audio::DspChain dsp_{48'000};
  std::mutex command_mutex_;
  std::condition_variable command_cv_, command_done_;
  std::filesystem::path path_;
  double seek_ = 0.0;
  Command command_ = Command::none;
  std::uint64_t request_id_ = 0, completed_id_ = 0;
  bool open_ok_ = false, shutting_down_ = false, worker_failed_ = false;
  std::thread producer_;
};

namespace {

const flutter::EncodableMap* ArgumentMap(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    const char* key = nullptr) {
  if (call.arguments() == nullptr) return nullptr;
  const auto* arguments = std::get_if<flutter::EncodableMap>(call.arguments());
  if (!arguments) return nullptr;
  if (!key) return arguments;
  const auto found = arguments->find(flutter::EncodableValue(key));
  return found == arguments->end()
             ? nullptr
             : std::get_if<flutter::EncodableMap>(&found->second);
}

bool NumberValue(const flutter::EncodableValue& value, double* output) {
  if (const auto* number = std::get_if<double>(&value)) {
    *output = *number;
    return true;
  }
  if (const auto* number = std::get_if<int32_t>(&value)) {
    *output = *number;
    return true;
  }
  if (const auto* number = std::get_if<int64_t>(&value)) {
    *output = static_cast<double>(*number);
    return true;
  }
  return false;
}

bool NumberArgument(const flutter::MethodCall<flutter::EncodableValue>& call,
                    const char* key, double* output) {
  const auto* values = ArgumentMap(call);
  if (!values) return false;
  const auto found = values->find(flutter::EncodableValue(key));
  return found != values->end() && NumberValue(found->second, output);
}

bool IntegerArgument(const flutter::MethodCall<flutter::EncodableValue>& call,
                     const char* key, int64_t* output) {
  const auto* values = ArgumentMap(call);
  if (!values) return false;
  const auto found = values->find(flutter::EncodableValue(key));
  if (found == values->end()) return false;
  if (const auto* value = std::get_if<int32_t>(&found->second)) {
    if (*value < 0) return false;
    *output = *value;
    return true;
  }
  if (const auto* value = std::get_if<int64_t>(&found->second)) {
    if (*value < 0) return false;
    *output = *value;
    return true;
  }
  return false;
}

bool ParseEffects(const flutter::MethodCall<flutter::EncodableValue>& call,
                  shipglows::audio::DspParameters* parameters) {
  const auto* effects = ArgumentMap(call, "effects");
  if (!effects || !parameters) return false;
  const auto number = [&](const char* name, float* target) {
    const auto found = effects->find(flutter::EncodableValue(name));
    if (found == effects->end()) return true;
    double value = 0;
    if (!NumberValue(found->second, &value) || !std::isfinite(value)) return false;
    *target = static_cast<float>(value);
    return true;
  };
  const auto boolean = [&](const char* name, bool* target) {
    const auto found = effects->find(flutter::EncodableValue(name));
    if (found == effects->end()) return true;
    const auto* value = std::get_if<bool>(&found->second);
    if (!value) return false;
    *target = *value;
    return true;
  };
  return boolean("eqEnabled", &parameters->eq_enabled) &&
      number("lowGainDb", &parameters->low_gain_db) &&
      number("midGainDb", &parameters->mid_gain_db) &&
      number("highGainDb", &parameters->high_gain_db) &&
      boolean("gateEnabled", &parameters->gate_enabled) &&
      number("gateThresholdDbfs", &parameters->gate_threshold_dbfs) &&
      number("gateReleaseMs", &parameters->gate_release_ms) &&
      boolean("compressorEnabled", &parameters->compressor_enabled) &&
      number("compressorThresholdDbfs", &parameters->compressor_threshold_dbfs) &&
      number("compressorRatio", &parameters->compressor_ratio) &&
      number("compressorAttackMs", &parameters->compressor_attack_ms) &&
      number("compressorReleaseMs", &parameters->compressor_release_ms);
}

flutter::EncodableValue PlaybackStatusValue(int64_t generation,
    const std::string& state, const WindowsLocalPlayback* source,
    double speed, const std::string& error = {},
    const WindowsWasapiPlayback* output = nullptr) {
  flutter::EncodableMap value;
  const auto output_status = output ? output->Status() : WasapiPlaybackResult{};
  const bool output_failed = output_status.failure != WasapiPlaybackFailure::none;
  const std::string reported_state = source && source->failed() ? "error"
      : (output_failed ? "error"
      : (source && source->completed() ? "completed" : state));
  const std::string reported_error = source && source->failed()
      ? "decode_failed"
      : (output_failed ? WasapiPlaybackFailureCode(output_status.failure) : error);
  value[flutter::EncodableValue("generation")] = flutter::EncodableValue(generation);
  value[flutter::EncodableValue("state")] = flutter::EncodableValue(reported_state);
  value[flutter::EncodableValue("positionSeconds")] = flutter::EncodableValue(
      source ? source->position() : 0.0);
  value[flutter::EncodableValue("durationSeconds")] = flutter::EncodableValue(
      source ? source->duration() : 0.0);
  value[flutter::EncodableValue("playbackSpeed")] = flutter::EncodableValue(speed);
  value[flutter::EncodableValue("errorCode")] = flutter::EncodableValue(reported_error);
  value[flutter::EncodableValue("nativeErrorHresult")] = flutter::EncodableValue(
      static_cast<int64_t>(output_failed ? output_status.hresult : 0));
  const auto metrics = output ? output->Metrics() : WasapiPlaybackMetrics{};
  value[flutter::EncodableValue("underruns")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.underruns));
  value[flutter::EncodableValue("p99CallbackMicroseconds")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.p99_callback_microseconds));
  return flutter::EncodableValue(value);
}

bool ApiVersionIsOne(const flutter::MethodCall<flutter::EncodableValue>& call) {
  int64_t version = 0;
  return IntegerArgument(call, "apiVersion", &version) && version == 1;
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
    const auto* input_endpoint = StringArgument(method_call, "inputEndpointId");
    const auto* output_endpoint = StringArgument(method_call, "outputEndpointId");
    bool microphone_enabled = true;
    if (const auto* args = std::get_if<flutter::EncodableMap>(method_call.arguments())) {
      const auto it = args->find(flutter::EncodableValue("microphoneEnabled"));
      if (it != args->end()) {
        if (const auto* enabled = std::get_if<bool>(&it->second)) microphone_enabled = *enabled;
      }
    }
    if (input_endpoint || output_endpoint || !microphone_enabled) {
      if (!capture_->SelectSources(microphone_enabled,
          input_endpoint ? Utf8Path(*input_endpoint).wstring() : std::wstring{},
          output_endpoint ? Utf8Path(*output_endpoint).wstring() : std::wstring{})) {
        result->Error("capture_sources_locked", "Sources are locked for the whole take.");
        return;
      }
    }
    if (!capture_->Start(path)) {
      const auto status = capture_->Status();
      result->Error(status.error_code.empty() ? "capture_start_failed"
                                              : status.error_code,
                    "The native WASAPI capture could not start.");
      return;
    }
    result->Success(CaptureStatusValue(capture_->Status()));
  } else if (method_call.method_name() == "getInputDevices" ||
             method_call.method_name() == "getOutputDevices") {
    const bool output = method_call.method_name() == "getOutputDevices";
    const auto endpoints = EnumerateInputEndpoints(output ? eRender : eCapture);
    if (!output) input_endpoint_ids_.clear();
    flutter::EncodableList devices;
    if (output && SupportsProcessLoopback()) {
      flutter::EncodableMap system;
      system[flutter::EncodableValue("id")] = flutter::EncodableValue(0);
      system[flutter::EncodableValue("name")] = flutter::EncodableValue("System audio");
      system[flutter::EncodableValue("endpointId")] = flutter::EncodableValue("shipglows:system-audio");
      system[flutter::EncodableValue("type")] = flutter::EncodableValue("systemAudio");
      system[flutter::EncodableValue("isExternal")] = flutter::EncodableValue(false);
      devices.emplace_back(system);
    }
    std::map<std::wstring, int> name_counts;
    std::map<std::wstring, int> name_ordinals;
    for (const auto& [endpoint_id, name] : endpoints) {
      static_cast<void>(endpoint_id);
      ++name_counts[name];
    }
    int id = 1;
    for (const auto& [endpoint_id, name] : endpoints) {
      if (!output) input_endpoint_ids_.push_back(endpoint_id);
      auto display_name = name;
      if (name_counts[name] > 1) {
        display_name += L" [" + std::to_wstring(++name_ordinals[name]) + L"]";
      }
      flutter::EncodableMap device;
      device[flutter::EncodableValue("id")] = flutter::EncodableValue(id++);
      device[flutter::EncodableValue("name")] =
          flutter::EncodableValue(Utf8String(display_name));
      device[flutter::EncodableValue("type")] =
          flutter::EncodableValue(output ? "systemAudio" : "microphone");
      device[flutter::EncodableValue("endpointId")] =
          flutter::EncodableValue(Utf8String(endpoint_id));
      device[flutter::EncodableValue("isExternal")] =
          flutter::EncodableValue(false);
      devices.emplace_back(device);
    }
    result->Success(flutter::EncodableValue(devices));
  } else if (method_call.method_name() == "selectInputDevice") {
    if (capture_->Status().state != shipglows::audio::SessionState::idle) {
      result->Error("capture_sources_locked", "Sources are locked for the whole take.");
      return;
    }
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
  } else if (method_call.method_name() == "loadPlayback") {
    if (!ApiVersionIsOne(method_call)) {
      result->Error("unsupported_playback_api", "Playback API version 1 is required.");
      return;
    }
    int64_t generation = 0;
    const auto* file = StringArgument(method_call, "localFilePath");
    double seek = 0, speed = 1;
    if (!IntegerArgument(method_call, "generation", &generation) || generation <= playback_generation_ ||
        !file || !NumberArgument(method_call, "seekSeconds", &seek) ||
        !NumberArgument(method_call, "playbackSpeed", &speed) ||
        !std::isfinite(seek) || seek < 0 || !std::isfinite(speed) ||
        speed < 0.5 || speed > 2.0) {
      result->Error("invalid_playback_request", "A newer generation, local file, valid seek, and speed in [0.5, 2.0] are required.");
      return;
    }
    const auto path = Utf8Path(*file);
    if (path.empty()) {
      result->Error("invalid_local_file_path", "A valid UTF-8 local file path is required.");
      return;
    }
    shipglows::audio::DspParameters effects;
    if (!ParseEffects(method_call, &effects)) {
      result->Error("invalid_playback_effects", "Playback effect values must be finite and use the version 1 field types.");
      return;
    }
    playback_output_.reset();
    playback_.reset();
    auto source = std::make_unique<WindowsLocalPlayback>();
    source->SetSpeed(speed);
    if (!source->SetEffects(effects) || !source->Open(path, seek)) {
      result->Error("playback_source_open_failed", "The local source could not be opened with the admitted Windows decoder profile.");
      return;
    }
    auto output = std::make_unique<WindowsWasapiPlayback>(
        &WindowsLocalPlayback::Render, source.get());
    playback_generation_ = generation;
    playback_speed_ = speed;
    playback_ = std::move(source);
    playback_output_ = std::move(output);
    // Loading only prepares and buffers the source. The caller decides when
    // audio becomes audible by issuing generation-matched playPlayback.
    playback_state_ = "ready";
    result->Success(PlaybackStatusValue(playback_generation_, playback_state_, playback_.get(), playback_speed_, {}, playback_output_.get()));
  } else if (method_call.method_name() == "seekPlayback" ||
             method_call.method_name() == "playPlayback" ||
             method_call.method_name() == "pausePlayback" ||
             method_call.method_name() == "stopPlayback" ||
             method_call.method_name() == "getPlaybackStatus" ||
             method_call.method_name() == "setPlaybackSpeed" ||
             method_call.method_name() == "setPlaybackEffects") {
    if (!ApiVersionIsOne(method_call)) {
      result->Error("unsupported_playback_api", "Playback API version 1 is required.");
      return;
    }
    int64_t generation = 0;
    const auto method = method_call.method_name();
    const bool valid_generation = IntegerArgument(method_call, "generation", &generation) &&
        (method == "stopPlayback" ? generation > playback_generation_
                                  : generation == playback_generation_);
    if (!valid_generation || !playback_ || !playback_output_) {
      result->Error("stale_playback_generation", "The playback command does not match the active generation.");
      return;
    }
    if (method == "getPlaybackStatus") {
      result->Success(PlaybackStatusValue(playback_generation_, playback_state_, playback_.get(), playback_speed_, {}, playback_output_.get()));
      return;
    } else if (method == "setPlaybackSpeed") {
      double speed = 0;
      if (!NumberArgument(method_call, "playbackSpeed", &speed) ||
          !std::isfinite(speed) || speed < 0.5 || speed > 2.0) {
        result->Error("invalid_playback_speed", "Playback speed must be in [0.5, 2.0].");
        return;
      }
      playback_speed_ = speed;
      playback_->SetSpeed(speed);
    } else if (method == "setPlaybackEffects") {
      shipglows::audio::DspParameters effects;
      if (!ParseEffects(method_call, &effects) || !playback_->SetEffects(effects)) {
        result->Error("invalid_playback_effects", "Playback effects are invalid or the bounded update queue is full.");
        return;
      }
    } else if (method == "seekPlayback") {
      double position = 0;
      if (!NumberArgument(method_call, "positionSeconds", &position) ||
          !std::isfinite(position) || position < 0 ||
          (playback_->duration() > 0 && position > playback_->duration())) {
        result->Error("invalid_playback_seek", "The seek position is outside the source duration.");
        return;
      }
      const bool resume = playback_state_ == "playing";
      const bool stay_paused = playback_state_ == "paused";
      if ((resume || stay_paused) && !playback_output_->Stop()) {
        result->Error("playback_stop_failed", "WASAPI could not quiesce the callback before seeking.");
        return;
      }
      if (!playback_->Seek(position)) {
        result->Error("playback_seek_failed", "The local decoder could not seek to that position.");
        return;
      }
      if (resume && !playback_output_->Start()) {
        result->Error("playback_output_start_failed", "WASAPI could not resume after seeking.");
        return;
      }
      if (stay_paused) {
        if (!playback_output_->Start() || !playback_output_->Pause()) {
          result->Error("playback_output_pause_failed", "WASAPI could not restore the paused state after seeking.");
          return;
        }
      }
    } else if (method == "playPlayback") {
      if (playback_->completed()) {
        if (playback_state_ == "playing" && !playback_output_->Stop()) {
          result->Error("playback_stop_failed", "WASAPI could not restart the completed source.");
          return;
        }
        if (!playback_->Seek(0)) {
          result->Error("playback_seek_failed", "The completed source could not rewind.");
          return;
        }
      }
      const auto started = playback_state_ == "paused"
          ? playback_output_->Resume() : playback_output_->Start();
      if (!started) {
        result->Error("playback_output_start_failed", "WASAPI could not start or resume playback.");
        return;
      }
      playback_state_ = "playing";
    } else if (method == "pausePlayback") {
      if (playback_state_ != "playing" || !playback_output_->Pause()) {
        result->Error("playback_pause_failed", "Playback is not active or WASAPI could not pause.");
        return;
      }
      playback_state_ = "paused";
    } else {
      if ((playback_state_ == "playing" || playback_state_ == "paused") &&
          !playback_output_->Stop()) {
        result->Error("playback_stop_failed", "WASAPI could not stop playback.");
        return;
      }
      // StopOutput is a generation boundary: fence all commands from the old
      // playback session as soon as the callback has been quiesced.
      playback_generation_ = generation;
      playback_state_ = "stopped";
      if (!playback_->Seek(0)) {
        result->Error("playback_stop_failed", "Playback stopped but the source could not reset to its beginning.");
        return;
      }
    }
    result->Success(PlaybackStatusValue(playback_generation_, playback_state_, playback_.get(), playback_speed_, {}, playback_output_.get()));
  } else {
    result->NotImplemented();
  }
}

}  // namespace shipglows_audio
