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
#include "shipglows/audio/synth_engine.hpp"
#include "shipglows/audio/visual_analysis_wire.hpp"
#include "shipglows/audio/visual_transform_contract.hpp"
#include "windows_wasapi_capture.h"
#include "windows_mf_decoder.h"
#include "windows_wasapi_playback.h"
#include "windows_output_bus.h"
#ifdef SHIPGLOWS_AUDIO_A0
#include "a0_windows_probe.h"
#endif

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
    const BusPlaybackOutput* output = nullptr) {
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
  value[flutter::EncodableValue("callbacks")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.callbacks));
  value[flutter::EncodableValue("maxCallbackMicroseconds")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.max_callback_microseconds));
  value[flutter::EncodableValue("bufferFrames")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.buffer_frames));
  // REFERENCE_TIME is 100 ns; Dart receives whole microseconds.
  value[flutter::EncodableValue("devicePeriodMicroseconds")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.default_period_100ns / 10));
  value[flutter::EncodableValue("minimumDevicePeriodMicroseconds")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.minimum_period_100ns / 10));
  value[flutter::EncodableValue("outputEndpointName")] =
      flutter::EncodableValue(output ? Utf8String(output->EndpointName()) : std::string{});
  value[flutter::EncodableValue("sampleRate")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.sample_rate));
  value[flutter::EncodableValue("endpointPeriod100ns")] =
      flutter::EncodableValue(static_cast<int64_t>(metrics.endpoint_period_100ns));
  return flutter::EncodableValue(value);
}

bool ApiVersionIsOne(const flutter::MethodCall<flutter::EncodableValue>& call) {
  int64_t version = 0;
  return IntegerArgument(call, "apiVersion", &version) && version == 1;
}

bool AnalysisApiVersionIsTwo(
    const flutter::MethodCall<flutter::EncodableValue>& call) {
  int64_t version = 0;
  return IntegerArgument(call, "analysisApiVersion", &version) && version == 2;
}

bool ExactArguments(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::initializer_list<const char*> keys) {
  const auto* arguments = ArgumentMap(call);
  if (!arguments || arguments->size() != keys.size()) return false;
  for (const auto* key : keys) {
    if (arguments->find(flutter::EncodableValue(key)) == arguments->end())
      return false;
  }
  return true;
}

bool ParseTransformConfig(const flutter::EncodableValue& value,
                          shipglows::audio::VisualAnalysisConfig* config,
                          std::string* digest) {
  const auto* map = std::get_if<flutter::EncodableMap>(&value);
  if (!map || map->size() != 7 || !config || !digest) return false;
  const auto get_number = [&](const char* key, double* out) {
    const auto it = map->find(flutter::EncodableValue(key));
    return it != map->end() && NumberValue(it->second, out) && std::isfinite(*out);
  };
  int64_t version = 0;
  const auto version_it = map->find(flutter::EncodableValue("version"));
  if (version_it == map->end()) return false;
  if (const auto* version32 = std::get_if<int32_t>(&version_it->second)) version = *version32;
  else if (const auto* version64 = std::get_if<int64_t>(&version_it->second)) version = *version64;
  if (version != 1 ||
      !get_number("sensitivity_db", &config->sensitivity_db) ||
      !get_number("amplitude_gamma", &config->gamma) ||
      !get_number("attack_ms", &config->attack_ms) ||
      !get_number("release_ms", &config->release_ms) ||
      !get_number("spectrum_floor_db", &config->floor_db) ||
      !get_number("spectrum_ceiling_db", &config->ceiling_db) ||
      config->sensitivity_db < -24 || config->sensitivity_db > 24 ||
      config->gamma < .25 || config->gamma > 2 ||
      config->attack_ms < 1 || config->attack_ms > 200 ||
      config->release_ms < 10 || config->release_ms > 2000 ||
      config->floor_db < -120 || config->floor_db > -20 ||
      config->ceiling_db < -6 || config->ceiling_db > 12 ||
      config->floor_db >= config->ceiling_db) return false;
  try {
    *digest = shipglows::audio::visual_transform_config_digest(*config);
  } catch (...) {
    return false;
  }
  return true;
}

bool SynthApiVersionIsOne(const flutter::MethodCall<flutter::EncodableValue>& call) {
  int64_t version = 0;
  return IntegerArgument(call, "synthApiVersion", &version) && version == 1;
}

bool ParseInstrument(const std::string* name, shipglows::audio::SynthInstrument* instrument) {
  if (!name || !instrument) return false;
  if (*name == "piano") {
    *instrument = shipglows::audio::SynthInstrument::piano;
    return true;
  }
  if (*name == "drums") {
    *instrument = shipglows::audio::SynthInstrument::drums;
    return true;
  }
  return false;
}

const char* InstrumentName(shipglows::audio::SynthInstrument instrument) {
  return instrument == shipglows::audio::SynthInstrument::drums ? "drums" : "piano";
}

flutter::EncodableValue LatencyValue(const shipglows::audio::SynthLatencyHistogram::Summary& summary) {
  flutter::EncodableMap value;
  value[flutter::EncodableValue("count")] = flutter::EncodableValue(static_cast<int64_t>(summary.count));
  value[flutter::EncodableValue("minMicroseconds")] = flutter::EncodableValue(static_cast<int64_t>(summary.min_us));
  value[flutter::EncodableValue("meanMicroseconds")] = flutter::EncodableValue(static_cast<int64_t>(summary.mean_us));
  value[flutter::EncodableValue("p50Microseconds")] = flutter::EncodableValue(static_cast<int64_t>(summary.p50_us));
  value[flutter::EncodableValue("p95Microseconds")] = flutter::EncodableValue(static_cast<int64_t>(summary.p95_us));
  value[flutter::EncodableValue("p99Microseconds")] = flutter::EncodableValue(static_cast<int64_t>(summary.p99_us));
  value[flutter::EncodableValue("maxMicroseconds")] = flutter::EncodableValue(static_cast<int64_t>(summary.max_us));
  return flutter::EncodableValue(value);
}

flutter::EncodableValue SynthStatusValue(int64_t generation, bool active,
                                         const WindowsOutputBus& bus) {
  const auto& synth = bus.Synth();
  const auto counters = synth.counters();
  const auto metrics = bus.Metrics();
  const auto status = bus.Status();
  const bool failed = status.failure != WasapiPlaybackFailure::none;
  flutter::EncodableMap value;
  const auto put = [&value](const char* key, flutter::EncodableValue item) {
    value[flutter::EncodableValue(key)] = std::move(item);
  };
  put("generation", flutter::EncodableValue(generation));
  put("state", flutter::EncodableValue(failed ? "error" : (active ? "running" : "stopped")));
  put("errorCode", flutter::EncodableValue(failed ? WasapiPlaybackFailureCode(status.failure) : ""));
  put("nativeErrorHresult", flutter::EncodableValue(static_cast<int64_t>(failed ? status.hresult : 0)));
  put("instrument", flutter::EncodableValue(InstrumentName(synth.instrument())));
  put("activeVoices", flutter::EncodableValue(static_cast<int64_t>(counters.active_voices)));
  put("peakActiveVoices", flutter::EncodableValue(static_cast<int64_t>(counters.peak_active_voices)));
  put("eventsPosted", flutter::EncodableValue(static_cast<int64_t>(counters.posted)));
  put("eventsRejected", flutter::EncodableValue(static_cast<int64_t>(counters.rejected)));
  put("eventsApplied", flutter::EncodableValue(static_cast<int64_t>(counters.applied)));
  put("eventsIgnored", flutter::EncodableValue(static_cast<int64_t>(counters.ignored)));
  put("stolenVoices", flutter::EncodableValue(static_cast<int64_t>(counters.stolen_voices)));
  put("queueLatency", LatencyValue(synth.queue_latency()));
  put("outputLatency", LatencyValue(synth.output_latency()));
  put("playbackAttached", flutter::EncodableValue(bus.PlaybackAttached()));
  put("lowLatency", flutter::EncodableValue(metrics.low_latency));
  put("sampleRate", flutter::EncodableValue(static_cast<int64_t>(metrics.sample_rate)));
  put("bufferFrames", flutter::EncodableValue(static_cast<int64_t>(metrics.buffer_frames)));
  put("enginePeriodFrames", flutter::EncodableValue(static_cast<int64_t>(metrics.engine_period_frames)));
  put("streamLatencyMicroseconds", flutter::EncodableValue(metrics.stream_latency_100ns / 10));
  const auto timing = bus.Timing();
  put("queuedFrames", timing.valid ? flutter::EncodableValue(static_cast<int64_t>(timing.queued_frames))
                                   : flutter::EncodableValue());
  put("callbacks", flutter::EncodableValue(static_cast<int64_t>(metrics.callbacks)));
  put("underruns", flutter::EncodableValue(static_cast<int64_t>(metrics.underruns)));
  put("p99CallbackMicroseconds", flutter::EncodableValue(static_cast<int64_t>(metrics.p99_callback_microseconds)));
  put("maxCallbackMicroseconds", flutter::EncodableValue(static_cast<int64_t>(metrics.max_callback_microseconds)));
  put("outputEndpointName", flutter::EncodableValue(Utf8String(bus.EndpointName())));
  return flutter::EncodableValue(value);
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

#ifdef SHIPGLOWS_AUDIO_A0
  auto probe = std::make_shared<std::unique_ptr<A0WindowsProbe>>();
  plugin->diagnostic_channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      registrar->messenger(), "shipglows_audio/a0", &flutter::StandardMethodCodec::GetInstance());
  plugin->diagnostic_channel_->SetMethodCallHandler([probe](const auto& call, auto result) {
    using V = flutter::EncodableValue;
    if(call.method_name()=="start") {
      const auto* mask=IntArgument(call,"mask");
      if(!mask || (*mask!=3 && *mask!=11 && *mask!=5 && *mask!=13)) {
        result->Error("a0_invalid_mask","Expected 3, 11, 5 or 13.");return;
      }
      bool enabled=true;
      bool silent_output=false;
      bool physical_stimulus=false;
      if(call.arguments())if(const auto* args=std::get_if<flutter::EncodableMap>(call.arguments())) {
        auto it=args->find(V("enabled"));if(it!=args->end())if(auto b=std::get_if<bool>(&it->second))enabled=*b;
        it=args->find(V("silent_output"));if(it!=args->end())if(auto b=std::get_if<bool>(&it->second))silent_output=*b;
        it=args->find(V("physical_stimulus"));if(it!=args->end())if(auto b=std::get_if<bool>(&it->second))physical_stimulus=*b;
      }
      if(physical_stimulus)silent_output=false;
      probe->reset();
      auto endpoints=EnumerateInputEndpoints(eRender);
      if(endpoints.empty()){result->Error("a0_no_endpoint","No active output endpoint.");return;}
      try {
        *probe=std::make_unique<A0WindowsProbe>(unsigned(*mask),enabled,silent_output,physical_stimulus);
        auto started=(*probe)->Start(endpoints.front().first);
        if(!started){probe->reset();result->Error(WasapiPlaybackFailureCode(started.failure),"A0 WASAPI start failed.");return;}
        flutter::EncodableMap reply;
        reply[V("endpoint_id")]=V(Utf8String(endpoints.front().first));
        reply[V("endpoint_name")]=V(Utf8String(endpoints.front().second));
        reply[V("mask")]=V(*mask);reply[V("enabled")]=V(enabled);reply[V("silent_output")]=V(silent_output);reply[V("physical_stimulus")]=V(physical_stimulus);result->Success(V(reply));
      } catch(const std::exception& error){probe->reset();result->Error("a0_start_failed",error.what());}
    } else if(call.method_name()=="poll") {
      if(!*probe){result->Error("a0_not_started","Start the diagnostic first.");return;}
      uint64_t after=0;
      if(call.arguments())if(const auto* args=std::get_if<flutter::EncodableMap>(call.arguments())) {
        auto it=args->find(V("after_sequence"));if(it!=args->end()) {
          if(auto n=std::get_if<int>(&it->second))after=uint64_t(std::max(0,*n));
          else if(auto wide=std::get_if<int64_t>(&it->second))after=uint64_t(std::max<int64_t>(0,*wide));
        }
      }
      try { result->Success((*probe)->Poll(after)); }
      catch(const std::exception& error){result->Error("a0_poll_failed",error.what());}
    } else if(call.method_name()=="stop") {
      if(*probe){
        (*probe)->Stop();
        try { result->Success((*probe)->Poll(0)); }
        catch(const std::exception& error){result->Error("a0_stop_snapshot_failed",error.what());}
      }else result->Success();
    } else result->NotImplemented();
  });
#endif

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto &call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

ShipglowsAudioPlugin::ShipglowsAudioPlugin(
    flutter::PluginRegistrarWindows* registrar)
    : registrar_(registrar),
      capture_(std::make_unique<WindowsWasapiCapture>()),
      output_bus_(std::make_unique<WindowsOutputBus>(true)) {
  if (registrar_ != nullptr) {
    window_proc_delegate_id_ = registrar_->RegisterTopLevelWindowProcDelegate(
        [this](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
          return HandleWindowProc(hwnd, message, wparam, lparam);
        });
  }
}

ShipglowsAudioPlugin::~ShipglowsAudioPlugin() {
  playback_output_.reset();
  ClearAnalysisStream(false);
  output_bus_.reset();
  playback_.reset();
  if (registrar_ != nullptr && window_proc_delegate_id_ >= 0) {
    registrar_->UnregisterTopLevelWindowProcDelegate(window_proc_delegate_id_);
  }
}

void ShipglowsAudioPlugin::ClearAnalysisStream(bool clean_eof) {
  if (output_bus_) output_bus_->ClearAnalysisStream();
  if (analysis_stream_) analysis_stream_->stop(clean_eof);
  analysis_stream_.reset();
  analysis_lease_stopped_ = true;
}

void ShipglowsAudioPlugin::ReleasePlaybackAnalysisLease(bool clean_eof) {
  if (analysis_source_ != AnalysisSource::playback_output) return;
  ClearAnalysisStream(clean_eof);
  analysis_source_ = AnalysisSource::none;
  analysis_generation_ = 0;
  analysis_lease_id_.clear();
  analysis_stream_id_.clear();
  analysis_source_identity_.clear();
  analysis_profile_.clear();
  analysis_transform_digest_.clear();
  analysis_feature_mask_ = 0;
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

void ShipglowsAudioPlugin::HandleSynthCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  using shipglows::audio::SynthEvent;
  using shipglows::audio::SynthEventType;
  const auto& method = method_call.method_name();
  int64_t generation = 0;
  if (!SynthApiVersionIsOne(method_call) ||
      !IntegerArgument(method_call, "generation", &generation) || generation <= 0) {
    result->Error("unsupported_synth_api", "Synth API version 1 with a positive generation is required.");
    return;
  }
  if (method == "startSynth") {
    shipglows::audio::SynthInstrument instrument;
    if (!ExactArguments(method_call, {"synthApiVersion", "generation", "instrument"}) ||
        !ParseInstrument(StringArgument(method_call, "instrument"), &instrument)) {
      result->Error("invalid_synth_request", "startSynth requires instrument piano or drums.");
      return;
    }
    if (generation < synth_generation_ || (generation == synth_generation_ && !synth_active_)) {
      result->Error("stale_synth_generation", "startSynth requires a newer generation.");
      return;
    }
    if (generation > synth_generation_) {
      // A new synth session ends the previous session's mix analysis lease.
      if (analysis_source_ == AnalysisSource::output_mix) {
        ClearAnalysisStream(false);
        analysis_source_ = AnalysisSource::none;
        analysis_generation_ = 0;
      }
      synth_generation_ = generation;
    }
    synth_active_ = true;
    SynthEvent voice;
    voice.type = SynthEventType::set_instrument;
    voice.instrument = instrument;
    const auto started = output_bus_->StartSynth();
    if (!started) {
      synth_active_ = false;
      result->Error(WasapiPlaybackFailureCode(started.failure), "The shared WASAPI output could not start.");
      return;
    }
    (void)output_bus_->PostSynth(voice);
    result->Success(SynthStatusValue(synth_generation_, synth_active_, *output_bus_));
    return;
  }
  if (generation != synth_generation_) {
    result->Error("stale_synth_generation", "The synth command does not match the active generation.");
    return;
  }
  if (method == "getSynthStatus") {
    if (!ExactArguments(method_call, {"synthApiVersion", "generation"})) {
      result->Error("invalid_synth_request", "getSynthStatus takes only its version and generation.");
      return;
    }
    result->Success(SynthStatusValue(synth_generation_, synth_active_, *output_bus_));
    return;
  }
  if (method == "stopSynth") {
    if (!ExactArguments(method_call, {"synthApiVersion", "generation"})) {
      result->Error("invalid_synth_request", "stopSynth takes only its version and generation.");
      return;
    }
    if (synth_active_) {
      if (analysis_source_ == AnalysisSource::output_mix) {
        ClearAnalysisStream(false);
        analysis_source_ = AnalysisSource::none;
        analysis_generation_ = 0;
      }
      output_bus_->StopSynth();
      synth_active_ = false;
    }
    result->Success(SynthStatusValue(synth_generation_, synth_active_, *output_bus_));
    return;
  }
  if (!synth_active_) {
    result->Error("stale_synth_generation", "The synth session is stopped.");
    return;
  }
  SynthEvent event;
  if (method == "noteOn" || method == "noteOff") {
    const bool on = method == "noteOn";
    const bool with_instrument = on
        ? ExactArguments(method_call, {"synthApiVersion", "generation", "midi", "velocity", "instrument"})
        : ExactArguments(method_call, {"synthApiVersion", "generation", "midi", "instrument"});
    const bool plain = on
        ? ExactArguments(method_call, {"synthApiVersion", "generation", "midi", "velocity"})
        : ExactArguments(method_call, {"synthApiVersion", "generation", "midi"});
    int64_t midi = -1;
    double velocity = 0;
    if ((!with_instrument && !plain) || !IntegerArgument(method_call, "midi", &midi) || midi > 127 ||
        (on && (!NumberArgument(method_call, "velocity", &velocity) || !std::isfinite(velocity) ||
                velocity < 0 || velocity > 1)) ||
        (with_instrument && !ParseInstrument(StringArgument(method_call, "instrument"), &event.instrument))) {
      result->Error("invalid_synth_request", "Notes need midi 0..127, velocity 0..1 and an optional instrument.");
      return;
    }
    event.type = on ? SynthEventType::note_on : SynthEventType::note_off;
    event.midi = static_cast<std::uint8_t>(midi);
    event.velocity = static_cast<float>(velocity);
    event.use_instrument = with_instrument;
  } else if (method == "click") {
    const bool* accent = nullptr;
    if (ExactArguments(method_call, {"synthApiVersion", "generation", "accent"}))
      accent = std::get_if<bool>(&ArgumentMap(method_call)->at(flutter::EncodableValue("accent")));
    if (!accent) {
      result->Error("invalid_synth_request", "click requires a boolean accent.");
      return;
    }
    event.type = SynthEventType::click;
    event.accent = *accent;
  } else if (method == "setVoice") {
    if (!ExactArguments(method_call, {"synthApiVersion", "generation", "instrument"}) ||
        !ParseInstrument(StringArgument(method_call, "instrument"), &event.instrument)) {
      result->Error("invalid_synth_request", "setVoice requires instrument piano or drums.");
      return;
    }
    event.type = SynthEventType::set_instrument;
  } else {
    result->NotImplemented();
    return;
  }
  result->Success(flutter::EncodableValue(output_bus_->PostSynth(event)));
}

void ShipglowsAudioPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  {
    const auto& name = method_call.method_name();
    if (name == "startSynth" || name == "stopSynth" || name == "noteOn" || name == "noteOff" ||
        name == "click" || name == "setVoice" || name == "getSynthStatus") {
      HandleSynthCall(method_call, std::move(result));
      return;
    }
  }
  if (method_call.method_name() == "getAnalysisCapabilities") {
    if (!ExactArguments(method_call, {"analysisApiVersion"}) ||
        !AnalysisApiVersionIsTwo(method_call)) {
      result->Error("unsupported_analysis_api", "Analysis API v2 requires its exact versioned argument map.");
      return;
    }
    flutter::EncodableMap caps;
    caps[flutter::EncodableValue("analysis_api_version")] = flutter::EncodableValue(2);
    caps[flutter::EncodableValue("available")] = flutter::EncodableValue(true);
    caps[flutter::EncodableValue("reason")] = flutter::EncodableValue();
    flutter::EncodableList sources;
    flutter::EncodableMap playback_source;
    playback_source[flutter::EncodableValue("id")] = flutter::EncodableValue("windows_playback_output_v1");
    playback_source[flutter::EncodableValue("available")] = flutter::EncodableValue(true);
    playback_source[flutter::EncodableValue("reason")] = flutter::EncodableValue();
    sources.emplace_back(playback_source);
    flutter::EncodableMap mix_source;
    mix_source[flutter::EncodableValue("id")] = flutter::EncodableValue("windows_output_mix_v1");
    mix_source[flutter::EncodableValue("available")] = flutter::EncodableValue(true);
    mix_source[flutter::EncodableValue("reason")] = flutter::EncodableValue();
    sources.emplace_back(mix_source);
    flutter::EncodableMap wav_source;
    wav_source[flutter::EncodableValue("id")] = flutter::EncodableValue("wav_host_v1");
    wav_source[flutter::EncodableValue("available")] = flutter::EncodableValue(false);
    wav_source[flutter::EncodableValue("reason")] = flutter::EncodableValue("offline_adapter_unavailable");
    sources.emplace_back(wav_source);
    caps[flutter::EncodableValue("sources")] = flutter::EncodableValue(sources);
    flutter::EncodableList profiles;
    for (const auto& [name, masks] : std::array<std::pair<const char*, std::array<int, 2>>, 2>{{
             {"sgav-music-1", {3, 11}}, {"sgav-speech-1", {5, 13}}}}) {
      flutter::EncodableMap profile;
      profile[flutter::EncodableValue("id")] = flutter::EncodableValue(name);
      flutter::EncodableList allowed_masks;
      flutter::EncodableMap record_bytes;
      for (const auto mask : masks) {
        allowed_masks.emplace_back(mask);
        record_bytes[flutter::EncodableValue(mask)] = flutter::EncodableValue(
            static_cast<int>(shipglows::audio::visual_analysis_record_bytes(mask)));
      }
      profile[flutter::EncodableValue("feature_masks")] = flutter::EncodableValue(allowed_masks);
      profile[flutter::EncodableValue("record_bytes")] = flutter::EncodableValue(record_bytes);
      profiles.emplace_back(profile);
    }
    caps[flutter::EncodableValue("profiles")] = flutter::EncodableValue(profiles);
    caps[flutter::EncodableValue("record_layout")] = flutter::EncodableValue("sgav-record-2-compact-speech-1");
    flutter::EncodableMap format;
    format[flutter::EncodableValue("sample_rate")] = flutter::EncodableValue(48000);
    format[flutter::EncodableValue("channels")] = flutter::EncodableValue(2);
    format[flutter::EncodableValue("layout")] = flutter::EncodableValue("interleaved");
    format[flutter::EncodableValue("sample_type")] = flutter::EncodableValue("float32");
    caps[flutter::EncodableValue("format")] = flutter::EncodableValue(format);
    caps[flutter::EncodableValue("max_block_frames")] = flutter::EncodableValue(16384);
    caps[flutter::EncodableValue("max_native_bytes")] = flutter::EncodableValue(4 * 1024 * 1024);
    caps[flutter::EncodableValue("history_records")] = flutter::EncodableValue(128);
    caps[flutter::EncodableValue("history_bytes")] = flutter::EncodableValue(1048576);
    caps[flutter::EncodableValue("history_span_frames")] = flutter::EncodableValue(61440);
    caps[flutter::EncodableValue("max_batch_records")] = flutter::EncodableValue(16);
    caps[flutter::EncodableValue("max_response_bytes")] = flutter::EncodableValue(131072);
    caps[flutter::EncodableValue("transform_versions")] = flutter::EncodableValue(
        flutter::EncodableList{flutter::EncodableValue("sgav-visual-transform-1")});
    result->Success(flutter::EncodableValue(caps));
  } else if (method_call.method_name() == "startAnalysis") {
    // The optional `source` selects the synth-fenced mix; without it the
    // lease keeps the original playback-fenced contract.
    const bool has_source = ExactArguments(method_call,
        {"analysisApiVersion", "generation", "profile", "featureMask", "transformConfig", "source"});
    if ((!has_source && !ExactArguments(method_call,
            {"analysisApiVersion", "generation", "profile", "featureMask", "transformConfig"})) ||
        !AnalysisApiVersionIsTwo(method_call)) {
      result->Error("unsupported_analysis_api", "startAnalysis requires the exact v2 argument map.");
      return;
    }
    const auto* source_name = has_source ? StringArgument(method_call, "source") : nullptr;
    if (has_source && (!source_name || (*source_name != "windows_playback_output_v1" &&
                                        *source_name != "windows_output_mix_v1"))) {
      result->Error("unsupported_analysis_source", "The analysis source is not supported on Windows.");
      return;
    }
    const auto source = source_name && *source_name == "windows_output_mix_v1"
        ? AnalysisSource::output_mix : AnalysisSource::playback_output;
    int64_t generation = 0, mask_value = 0;
    const auto* profile = StringArgument(method_call, "profile");
    const auto* arguments = ArgumentMap(method_call);
    const auto config_it = arguments->find(flutter::EncodableValue("transformConfig"));
    shipglows::audio::VisualAnalysisConfig config;
    std::string digest;
    const bool config_valid = ParseTransformConfig(config_it->second, &config, &digest);
    const bool mask_valid = IntegerArgument(method_call, "featureMask", &mask_value) &&
        ((profile && *profile == "sgav-music-1" && (mask_value == 3 || mask_value == 11)) ||
         (profile && *profile == "sgav-speech-1" && (mask_value == 5 || mask_value == 13)));
    if (!IntegerArgument(method_call, "generation", &generation) || generation <= 0 ||
        (source == AnalysisSource::playback_output &&
         (generation != playback_generation_ || !playback_ || !playback_output_)) ||
        (source == AnalysisSource::output_mix &&
         (generation != synth_generation_ || !synth_active_))) {
      result->Error("stale_analysis_generation", source == AnalysisSource::output_mix
          ? "Analysis generation must match a running synth session."
          : "Analysis generation must match a loaded playback session.");
      return;
    }
    if (!mask_valid || !config_valid) {
      result->Error("invalid_analysis_configuration", "Profile, feature mask, and transform configuration must match the v2 contract.");
      return;
    }
    if (analysis_stream_ && !analysis_lease_stopped_ && analysis_source_ == source &&
        analysis_generation_ == generation && analysis_profile_ == *profile && analysis_feature_mask_ == static_cast<unsigned>(mask_value) &&
        analysis_transform_digest_ == digest) {
      flutter::EncodableMap lease;
      lease[flutter::EncodableValue("analysis_api_version")] = flutter::EncodableValue(2);
      lease[flutter::EncodableValue("generation")] = flutter::EncodableValue(generation);
      lease[flutter::EncodableValue("lease_id")] = flutter::EncodableValue(analysis_lease_id_);
      lease[flutter::EncodableValue("stream_id")] = flutter::EncodableValue(analysis_stream_id_);
      lease[flutter::EncodableValue("source_identity")] = flutter::EncodableValue(analysis_source_identity_);
      lease[flutter::EncodableValue("profile")] = flutter::EncodableValue(analysis_profile_);
      lease[flutter::EncodableValue("feature_mask")] = flutter::EncodableValue(static_cast<int>(analysis_feature_mask_));
      lease[flutter::EncodableValue("transform_digest")] = flutter::EncodableValue(analysis_transform_digest_);
      lease[flutter::EncodableValue("max_block_frames")] = flutter::EncodableValue(16384);
      lease[flutter::EncodableValue("max_batch_records")] = flutter::EncodableValue(16);
      lease[flutter::EncodableValue("max_response_bytes")] = flutter::EncodableValue(131072);
      result->Success(flutter::EncodableValue(lease));
      return;
    }
    ClearAnalysisStream(false);
    static std::atomic<std::uint64_t> next_lease{1};
    const auto serial = next_lease.fetch_add(1, std::memory_order_relaxed);
    auto stream = std::make_unique<shipglows::audio::AnalysisStream>(
        static_cast<unsigned>(mask_value), config, 16384);
    if (stream->bytes() > 4 * 1024 * 1024) {
      result->Error("analysis_memory_limit", "The requested analysis stream exceeds its advertised native memory budget.");
      return;
    }
    analysis_stream_ = std::move(stream);
    analysis_source_ = source;
    analysis_generation_ = generation;
    const std::string source_prefix = source == AnalysisSource::output_mix
        ? "windows-output-mix" : "windows-playback";
    analysis_lease_id_ = "analysis-lease-" + std::to_string(serial);
    analysis_stream_id_ = source_prefix + "-" + std::to_string(generation) + "-" + std::to_string(serial);
    analysis_source_identity_ = source_prefix + "-v1-" + std::to_string(generation);
    analysis_profile_ = *profile;
    analysis_feature_mask_ = static_cast<unsigned>(mask_value);
    analysis_transform_digest_ = digest;
    analysis_lease_stopped_ = false;
    output_bus_->SetAnalysisStream(analysis_stream_.get());
    flutter::EncodableMap lease;
    lease[flutter::EncodableValue("analysis_api_version")] = flutter::EncodableValue(2);
    lease[flutter::EncodableValue("generation")] = flutter::EncodableValue(generation);
    lease[flutter::EncodableValue("lease_id")] = flutter::EncodableValue(analysis_lease_id_);
    lease[flutter::EncodableValue("stream_id")] = flutter::EncodableValue(analysis_stream_id_);
    lease[flutter::EncodableValue("source_identity")] = flutter::EncodableValue(analysis_source_identity_);
    lease[flutter::EncodableValue("profile")] = flutter::EncodableValue(analysis_profile_);
    lease[flutter::EncodableValue("feature_mask")] = flutter::EncodableValue(static_cast<int>(analysis_feature_mask_));
    lease[flutter::EncodableValue("transform_digest")] = flutter::EncodableValue(analysis_transform_digest_);
    lease[flutter::EncodableValue("max_block_frames")] = flutter::EncodableValue(16384);
    lease[flutter::EncodableValue("max_batch_records")] = flutter::EncodableValue(16);
    lease[flutter::EncodableValue("max_response_bytes")] = flutter::EncodableValue(131072);
    result->Success(flutter::EncodableValue(lease));
  } else if (method_call.method_name() == "getAnalysisBatch") {
    if (!ExactArguments(method_call, {"analysisApiVersion", "generation", "leaseId", "afterSequence"}) ||
        !AnalysisApiVersionIsTwo(method_call)) {
      result->Error("unsupported_analysis_api", "getAnalysisBatch requires the exact v2 argument map.");
      return;
    }
    int64_t generation = 0, after = 0;
    const auto* lease_id = StringArgument(method_call, "leaseId");
    if (!IntegerArgument(method_call, "generation", &generation) || generation != analysis_generation_ ||
        (analysis_source_ == AnalysisSource::playback_output && generation != playback_generation_) ||
        !IntegerArgument(method_call, "afterSequence", &after) ||
        static_cast<std::uint64_t>(after) > 9007199254740991ULL ||
        !lease_id || *lease_id != analysis_lease_id_ || !analysis_stream_) {
      result->Error("stale_analysis_lease", "Analysis lease, generation, or cursor is invalid.");
      return;
    }
    const bool playback_lease = analysis_source_ == AnalysisSource::playback_output;
    if (!analysis_lease_stopped_ && playback_lease && playback_ && playback_->completed()) {
      output_bus_->ClearAnalysisStream();
      analysis_stream_->stop(true);
      analysis_lease_stopped_ = true;
    }
    shipglows::audio::AnalysisBatchSnapshot batch;
    try {
      batch = analysis_stream_->poll(static_cast<std::uint64_t>(after));
    } catch (const std::exception&) {
      result->Error("invalid_analysis_cursor", "Analysis cursor is ahead of the committed producer sequence.");
      return;
    }
    if (playback_lease && !analysis_lease_stopped_ && playback_state_ == "paused") batch.state = "paused";
    if (playback_lease && playback_ && playback_->failed()) batch.state = "failed";
    if (!playback_lease && !analysis_lease_stopped_ &&
        output_bus_->Status().failure != WasapiPlaybackFailure::none) batch.state = "failed";
    static const auto observation_origin = std::chrono::steady_clock::now();
    const auto observation_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - observation_origin).count());
    constexpr std::uint64_t kMaxSafeInteger = 9007199254740991ULL;
    if (batch.continuity_id > kMaxSafeInteger || batch.origin_frame > kMaxSafeInteger ||
        batch.first_sequence > kMaxSafeInteger || batch.last_sequence > kMaxSafeInteger ||
        batch.next_sequence_cursor > kMaxSafeInteger || batch.skipped_delivery_records > kMaxSafeInteger ||
        batch.dropped_pcm_frames > kMaxSafeInteger || observation_ns > kMaxSafeInteger) {
      result->Error("analysis_integer_limit", "Analysis metadata exceeded the safe channel integer range.");
      return;
    }
    std::vector<std::uint8_t> payload;
    const auto record_bytes = shipglows::audio::visual_analysis_record_bytes(analysis_feature_mask_);
    payload.reserve(batch.records.size() * record_bytes);
    for (const auto& record : batch.records) {
      auto encoded = shipglows::audio::encode_visual_analysis_record(record, analysis_feature_mask_);
      payload.insert(payload.end(), encoded.begin(), encoded.end());
    }
    if (payload.size() + 4096 > 131072) {
      result->Error("analysis_response_limit", "The encoded analysis batch exceeds the v2 response budget.");
      return;
    }
    flutter::EncodableMap response;
    const auto put = [&response](const char* key, flutter::EncodableValue value) {
      response[flutter::EncodableValue(key)] = std::move(value);
    };
    put("analysis_api_version", flutter::EncodableValue(2));
    put("generation", flutter::EncodableValue(generation));
    put("lease_id", flutter::EncodableValue(analysis_lease_id_));
    put("stream_id", flutter::EncodableValue(analysis_stream_id_));
    put("source_identity", flutter::EncodableValue(analysis_source_identity_));
    put("profile", flutter::EncodableValue(analysis_profile_));
    put("feature_mask", flutter::EncodableValue(static_cast<int>(analysis_feature_mask_)));
    put("transform_digest", flutter::EncodableValue(analysis_transform_digest_));
    put("state", flutter::EncodableValue(batch.state));
    put("continuity_id", flutter::EncodableValue(static_cast<int64_t>(batch.continuity_id)));
    put("run_origin_frame", batch.has_origin
        ? flutter::EncodableValue(static_cast<int64_t>(batch.origin_frame))
        : flutter::EncodableValue());
    put("record_layout", flutter::EncodableValue("sgav-record-2-compact-speech-1"));
    put("first_sequence", flutter::EncodableValue(static_cast<int64_t>(batch.first_sequence)));
    put("last_sequence", flutter::EncodableValue(static_cast<int64_t>(batch.last_sequence)));
    put("next_sequence_cursor", flutter::EncodableValue(static_cast<int64_t>(batch.next_sequence_cursor)));
    put("delivery_resync", flutter::EncodableValue(batch.delivery_resync));
    put("resync_reason", flutter::EncodableValue(batch.resync_reason));
    put("skipped_delivery_records", flutter::EncodableValue(static_cast<int64_t>(batch.skipped_delivery_records)));
    put("pcm_gap", flutter::EncodableValue(batch.pcm_gap));
    put("dropped_pcm_frames", flutter::EncodableValue(static_cast<int64_t>(batch.dropped_pcm_frames)));
    put("native_observation_ns", flutter::EncodableValue(static_cast<int64_t>(observation_ns)));
    put("timing", flutter::EncodableValue());
    put("record_bytes", flutter::EncodableValue(static_cast<int>(record_bytes)));
    put("record_count", flutter::EncodableValue(static_cast<int>(batch.records.size())));
    put("records", flutter::EncodableValue(std::move(payload)));
    result->Success(flutter::EncodableValue(response));
  } else if (method_call.method_name() == "stopAnalysis") {
    if (!ExactArguments(method_call, {"analysisApiVersion", "generation", "leaseId"}) ||
        !AnalysisApiVersionIsTwo(method_call)) {
      result->Error("unsupported_analysis_api", "stopAnalysis requires the exact v2 argument map.");
      return;
    }
    int64_t generation = 0;
    const auto* lease_id = StringArgument(method_call, "leaseId");
    if (!IntegerArgument(method_call, "generation", &generation) || generation != analysis_generation_ ||
        !lease_id || *lease_id != analysis_lease_id_ || !analysis_stream_) {
      result->Error("stale_analysis_lease", "Analysis lease or generation is stale.");
      return;
    }
    if (!analysis_lease_stopped_) {
      output_bus_->ClearAnalysisStream();
      analysis_stream_->stop(false);
      analysis_lease_stopped_ = true;
    }
    result->Success(flutter::EncodableValue(true));
  } else if (method_call.method_name() == "getEngineInfo") {
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
    ReleasePlaybackAnalysisLease(false);
    playback_.reset();
    auto source = std::make_unique<WindowsLocalPlayback>();
    source->SetSpeed(speed);
    if (!source->SetEffects(effects) || !source->Open(path, seek)) {
      result->Error("playback_source_open_failed", "The local source could not be opened with the admitted Windows decoder profile.");
      return;
    }
    auto output = std::make_unique<BusPlaybackOutput>(
        output_bus_.get(), &WindowsLocalPlayback::Render, source.get());
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
      if (analysis_stream_ && analysis_source_ == AnalysisSource::playback_output)
        analysis_stream_->mark_discontinuity();
      // A paused session stays detached from the bus, so it stays silent.
      if (resume && !playback_output_->Start()) {
        result->Error("playback_output_start_failed", "WASAPI could not resume after seeking.");
        return;
      }
    } else if (method == "playPlayback") {
      if (playback_->completed()) {
        if (playback_state_ == "playing" && !playback_output_->Stop()) {
          result->Error("playback_stop_failed", "WASAPI could not restart the completed source.");
          return;
        }
        ReleasePlaybackAnalysisLease(true);
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
      ReleasePlaybackAnalysisLease(false);
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
