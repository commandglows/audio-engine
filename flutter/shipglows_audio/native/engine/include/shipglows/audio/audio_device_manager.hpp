#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "shipglows/audio/audio_types.hpp"

namespace shipglows::audio {

enum class DeviceRoute : std::uint8_t {
  built_in,
  wired,
  usb,
  bluetooth,
  virtual_device,
  unknown,
};

enum class PermissionState : std::uint8_t {
  granted,
  denied,
  restricted,
  unknown,
};

enum class FallbackReason : std::uint8_t {
  none,
  preferred_missing,
  preferred_open_failed,
  unsupported_configuration,
};

struct AudioInputDevice final {
  std::string stable_id;
  std::string display_name;
  std::string backend;
  DeviceRoute route = DeviceRoute::unknown;
  PermissionState permission = PermissionState::unknown;
  bool is_system_default = false;
  bool supports_hardware_timestamps = false;
  bool supports_native_xruns = false;
  std::uint16_t max_input_channels = 0;
  std::uint32_t default_buffer_frames = 0;
  std::vector<std::uint32_t> sample_rates;
  std::vector<std::uint32_t> buffer_sizes;
  std::vector<SampleFormat> sample_formats;
};

struct AudioDeviceSetup final {
  std::string device_id;
  std::uint32_t sample_rate = 0;
  std::uint32_t buffer_frames = 0;
  std::uint16_t channel_count = 1;
  SampleFormat sample_format = SampleFormat::float32;
};

struct DeviceOpenResult final {
  bool opened = false;
  AudioDeviceSetup effective_setup;
  std::string error_code;
};

struct DeviceManagerSnapshot final {
  std::vector<AudioInputDevice> devices;
  std::optional<AudioDeviceSetup> preferred_setup;
  std::optional<AudioDeviceSetup> effective_setup;
  FallbackReason fallback_reason = FallbackReason::none;
  std::string error_code;
  std::uint64_t generation = 0;
};

struct AudioCallbackContext final {
  std::uint64_t host_time_ns = 0;
  std::uint64_t engine_frame_position = 0;
  std::uint64_t native_xrun_count = 0;
  std::uint64_t lifecycle_generation = 0;
  std::uint32_t frame_count = 0;
  bool has_hardware_timestamp = false;
  bool has_native_xrun_count = false;
};

class AudioDeviceProvider {
 public:
  virtual ~AudioDeviceProvider() = default;
  [[nodiscard]] virtual std::vector<AudioInputDevice> enumerate_inputs() = 0;
  [[nodiscard]] virtual DeviceOpenResult open_input(
      const AudioDeviceSetup& setup) = 0;
  virtual void close_input() noexcept = 0;
};

class AudioDeviceManager final {
 public:
  explicit AudioDeviceManager(AudioDeviceProvider& provider) noexcept;

  [[nodiscard]] DeviceManagerSnapshot refresh();
  [[nodiscard]] DeviceOpenResult open(
      std::optional<AudioDeviceSetup> preferred_setup);
  void close() noexcept;
  [[nodiscard]] const DeviceManagerSnapshot& snapshot() const noexcept;

 private:
  [[nodiscard]] const AudioInputDevice* find_device(
      const std::string& stable_id) const noexcept;
  [[nodiscard]] const AudioInputDevice* default_device() const noexcept;
  [[nodiscard]] AudioDeviceSetup default_setup(
      const AudioInputDevice& device) const;

  AudioDeviceProvider& provider_;
  DeviceManagerSnapshot snapshot_;
};

}  // namespace shipglows::audio
