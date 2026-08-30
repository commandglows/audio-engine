#include "shipglows/audio/audio_device_manager.hpp"

#include <algorithm>

namespace shipglows::audio {
namespace {

template <typename Value>
Value first_or(const std::vector<Value>& values, Value fallback) {
  return values.empty() ? fallback : values.front();
}

}  // namespace

AudioDeviceManager::AudioDeviceManager(AudioDeviceProvider& provider) noexcept
    : provider_(provider) {}

DeviceManagerSnapshot AudioDeviceManager::refresh() {
  snapshot_.devices = provider_.enumerate_inputs();
  return snapshot_;
}

DeviceOpenResult AudioDeviceManager::open(
    std::optional<AudioDeviceSetup> preferred_setup) {
  close();
  snapshot_.devices = provider_.enumerate_inputs();
  snapshot_.preferred_setup = preferred_setup;
  snapshot_.effective_setup.reset();
  snapshot_.fallback_reason = FallbackReason::none;
  snapshot_.error_code.clear();

  if (preferred_setup.has_value()) {
    if (find_device(preferred_setup->device_id) != nullptr) {
      auto preferred_result = provider_.open_input(*preferred_setup);
      if (preferred_result.opened) {
        snapshot_.effective_setup = preferred_result.effective_setup;
        ++snapshot_.generation;
        return preferred_result;
      }
      snapshot_.fallback_reason = FallbackReason::preferred_open_failed;
      snapshot_.error_code = preferred_result.error_code;
    } else {
      snapshot_.fallback_reason = FallbackReason::preferred_missing;
      snapshot_.error_code = "preferred_device_missing";
    }
  }

  const auto* fallback = default_device();
  if (fallback == nullptr) {
    return {.opened = false,
            .effective_setup = {},
            .error_code = snapshot_.devices.empty() ? "no_input_device"
                                                    : snapshot_.error_code};
  }

  auto fallback_result = provider_.open_input(default_setup(*fallback));
  if (fallback_result.opened) {
    snapshot_.effective_setup = fallback_result.effective_setup;
    ++snapshot_.generation;
  }
  if (!fallback_result.error_code.empty()) {
    snapshot_.error_code = fallback_result.error_code;
  }
  return fallback_result;
}

void AudioDeviceManager::close() noexcept {
  provider_.close_input();
  snapshot_.effective_setup.reset();
}

const DeviceManagerSnapshot& AudioDeviceManager::snapshot() const noexcept {
  return snapshot_;
}

const AudioInputDevice* AudioDeviceManager::find_device(
    const std::string& stable_id) const noexcept {
  const auto found = std::find_if(
      snapshot_.devices.begin(), snapshot_.devices.end(),
      [&stable_id](const AudioInputDevice& device) {
        return device.stable_id == stable_id;
      });
  return found == snapshot_.devices.end() ? nullptr : &*found;
}

const AudioInputDevice* AudioDeviceManager::default_device() const noexcept {
  const auto found = std::find_if(
      snapshot_.devices.begin(), snapshot_.devices.end(),
      [](const AudioInputDevice& device) { return device.is_system_default; });
  if (found != snapshot_.devices.end()) {
    return &*found;
  }
  return snapshot_.devices.empty() ? nullptr : &snapshot_.devices.front();
}

AudioDeviceSetup AudioDeviceManager::default_setup(
    const AudioInputDevice& device) const {
  return {
      .device_id = device.stable_id,
      .sample_rate = first_or(device.sample_rates, std::uint32_t{48'000}),
      .buffer_frames = device.default_buffer_frames != 0
                           ? device.default_buffer_frames
                           : first_or(device.buffer_sizes, std::uint32_t{0}),
      .channel_count = std::max<std::uint16_t>(
          1, std::min<std::uint16_t>(2, device.max_input_channels)),
      .sample_format =
          first_or(device.sample_formats, SampleFormat::float32),
  };
}

}  // namespace shipglows::audio
