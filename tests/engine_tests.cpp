#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "shipglows/audio/audio_device_manager.hpp"
#include "shipglows/audio/capture_session.hpp"
#include "shipglows/audio/segmented_pcm_store.hpp"
#include "shipglows/audio/segmented_wav_store.hpp"
#include "shipglows/audio/sha256.hpp"
#include "shipglows/audio/spsc_audio_ring_buffer.hpp"
#include "shipglows/audio/pcm_analysis.hpp"

namespace {

using shipglows::audio::AudioFormat;
using shipglows::audio::AudioDeviceManager;
using shipglows::audio::AudioDeviceProvider;
using shipglows::audio::AudioDeviceSetup;
using shipglows::audio::AudioInputDevice;
using shipglows::audio::CaptureSession;
using shipglows::audio::DeviceOpenResult;
using shipglows::audio::DeviceRoute;
using shipglows::audio::FallbackReason;
using shipglows::audio::PermissionState;
using shipglows::audio::SampleFormat;
using shipglows::audio::SegmentedPcmStore;
using shipglows::audio::SegmentedWavStore;
using shipglows::audio::SessionState;
using shipglows::audio::Sha256;
using shipglows::audio::SpscAudioRingBuffer;

class FakeDeviceProvider final : public AudioDeviceProvider {
 public:
  std::vector<AudioInputDevice> devices;
  std::string rejected_device;
  std::vector<std::string> open_attempts;

  std::vector<AudioInputDevice> enumerate_inputs() override { return devices; }

  DeviceOpenResult open_input(const AudioDeviceSetup& setup) override {
    open_attempts.push_back(setup.device_id);
    if (setup.device_id == rejected_device) {
      return {.opened = false,
              .effective_setup = {},
              .error_code = "device_open_failed"};
    }
    return {.opened = true, .effective_setup = setup, .error_code = {}};
  }

  void close_input() noexcept override {}
};

AudioInputDevice fake_device(std::string id, bool is_default) {
  return {
      .stable_id = std::move(id),
      .display_name = "Test input",
      .backend = "fake",
      .route = DeviceRoute::built_in,
      .permission = PermissionState::granted,
      .is_system_default = is_default,
      .supports_hardware_timestamps = true,
      .supports_native_xruns = true,
      .max_input_channels = 2,
      .default_buffer_frames = 256,
      .sample_rates = {48'000},
      .buffer_sizes = {128, 256},
      .sample_formats = {SampleFormat::float32},
  };
}

void test_device_manager_opens_preferred_device() {
  FakeDeviceProvider provider;
  provider.devices = {fake_device("default", true),
                      fake_device("preferred", false)};
  AudioDeviceManager manager(provider);
  const AudioDeviceSetup preferred{
      .device_id = "preferred",
      .sample_rate = 48'000,
      .buffer_frames = 128,
      .channel_count = 2,
      .sample_format = SampleFormat::float32,
  };
  const auto result = manager.open(preferred);
  assert(result.opened);
  assert(provider.open_attempts.size() == 1);
  assert(provider.open_attempts.front() == "preferred");
  assert(manager.snapshot().fallback_reason == FallbackReason::none);
  assert(manager.snapshot().generation == 1);
}

void test_device_manager_falls_back_without_hiding_failure() {
  FakeDeviceProvider provider;
  provider.devices = {fake_device("default", true),
                      fake_device("preferred", false)};
  provider.rejected_device = "preferred";
  AudioDeviceManager manager(provider);
  const auto result = manager.open(AudioDeviceSetup{
      .device_id = "preferred",
      .sample_rate = 96'000,
      .buffer_frames = 64,
      .channel_count = 2,
      .sample_format = SampleFormat::float32,
  });
  assert(result.opened);
  assert(provider.open_attempts.size() == 2);
  assert(provider.open_attempts[0] == "preferred");
  assert(provider.open_attempts[1] == "default");
  assert(manager.snapshot().preferred_setup->device_id == "preferred");
  assert(manager.snapshot().effective_setup->device_id == "default");
  assert(manager.snapshot().fallback_reason ==
         FallbackReason::preferred_open_failed);
}

void test_session_state_machine() {
  CaptureSession session;
  assert(!session.start());
  assert(session.prepare({48'000, 1, SampleFormat::int16}));
  assert(session.start());
  session.count_captured_frames(480);
  session.count_dropped_frames(2);
  session.count_discontinuity();
  session.count_native_xruns(2);
  session.count_timestamp_gap_frames(12);
  session.count_writer_stall();
  session.count_route_change();
  assert(session.request_stop());
  assert(session.finish());
  assert(session.state() == SessionState::stopped);
  assert(session.metrics().frames_captured == 480);
  assert(session.metrics().frames_dropped == 2);
  assert(session.metrics().discontinuities == 1);
  assert(session.metrics().ring_overflow_frames == 2);
  assert(session.metrics().native_xruns == 2);
  assert(session.metrics().timestamp_gap_frames == 12);
  assert(session.metrics().writer_stalls == 1);
  assert(session.metrics().route_changes == 1);
}

void test_ring_buffer_wraps_without_overwrite() {
  SpscAudioRingBuffer<std::int16_t> ring(4);
  const std::array<std::int16_t, 3> first{1, 2, 3};
  assert(ring.push(first) == 3);
  std::array<std::int16_t, 2> head{};
  assert(ring.pop(head) == 2);
  assert((head == std::array<std::int16_t, 2>{1, 2}));
  const std::array<std::int16_t, 4> second{4, 5, 6, 7};
  assert(ring.push(second) == 3);
  std::array<std::int16_t, 4> tail{};
  assert(ring.pop(tail) == 4);
  assert((tail == std::array<std::int16_t, 4>{3, 4, 5, 6}));
}

void test_segment_rotation_and_recovery() {
  const auto root = std::filesystem::temp_directory_path() /
                    "shipglows-audio-engine-tests";
  std::filesystem::remove_all(root);
  const AudioFormat format{48'000, 1, SampleFormat::int16};
  {
    SegmentedPcmStore store(root, format, 4);
    const std::array<std::int16_t, 10> samples{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    store.append(std::as_bytes(std::span(samples)));
    store.finalize();
    assert(store.segments().size() == 3);
    assert(store.segments()[0].frames == 4);
    assert(store.segments()[2].frames == 2);
  }
  {
    std::ofstream damaged(root / "segment-000003.pcm", std::ios::binary);
    const std::array<char, 3> bytes{1, 2, 3};
    damaged.write(bytes.data(), bytes.size());
  }
  const auto recovery = SegmentedPcmStore::recover(root, format);
  assert(recovery.recovered_frames == 11);
  assert(recovery.discarded_bytes == 1);
  std::filesystem::remove_all(root);
}

void test_clipping_analysis() {
  const std::array<std::int16_t, 5> samples{0, 12'000, 32'760, -32'768, 1};
  const auto clipped = shipglows::audio::count_clipped_samples(
      std::as_bytes(std::span(samples)),
      AudioFormat{48'000, 1, SampleFormat::int16});
  assert(clipped == 2);
}

void test_sha256_known_vectors() {
  Sha256 empty;
  assert(Sha256::hex(empty.finalize()) ==
         "e3b0c44298fc1c149afbf4c8996fb924"
         "27ae41e4649b934ca495991b7852b855");

  Sha256 abc;
  const std::array<char, 3> text{'a', 'b', 'c'};
  abc.update(std::as_bytes(std::span(text)));
  assert(Sha256::hex(abc.finalize()) ==
         "ba7816bf8f01cfea414140de5dae2223"
         "b00361a396177a9cb410ff61f20015ad");
}

void test_segmented_wav_store_is_self_describing_and_journaled() {
  const auto root = std::filesystem::temp_directory_path() /
                    "shipglows-audio-engine-wav-tests";
  std::filesystem::remove_all(root);
  const AudioFormat format{48'000, 2, SampleFormat::int16};
  {
    SegmentedWavStore store(root, format, 4);
    const std::array<std::int16_t, 20> samples{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    store.append(std::as_bytes(std::span(samples)));
    store.finalize();
    assert(store.segments().size() == 3);
    assert(store.segments()[0].start_frame == 0);
    assert(store.segments()[1].start_frame == 4);
    assert(store.segments()[2].frames == 2);
    assert(store.total_frames() == 10);
    assert(store.segments()[0].sha256.size() == 64);
  }
  {
    std::ifstream wav(root / "segment-000000.wav", std::ios::binary);
    std::array<char, 12> identity{};
    wav.read(identity.data(), identity.size());
    assert(std::string(identity.data(), 4) == "RIFF");
    assert(std::string(identity.data() + 8, 4) == "WAVE");
    assert(std::filesystem::file_size(root / "segment-000000.wav") == 60);
  }
  {
    std::ifstream journal(root / "journal.sga");
    const std::string content((std::istreambuf_iterator<char>(journal)),
                              std::istreambuf_iterator<char>());
    assert(content.find("schema=shipglows-audio-session/2") !=
           std::string::npos);
    assert(content.find("event=session_complete,total_frames=10") !=
           std::string::npos);
    assert(content.find("segment=0,0,4,60,") != std::string::npos);
  }
  std::filesystem::remove_all(root);
}

}  // namespace

int main() {
  test_device_manager_opens_preferred_device();
  test_device_manager_falls_back_without_hiding_failure();
  test_session_state_machine();
  test_ring_buffer_wraps_without_overwrite();
  test_segment_rotation_and_recovery();
  test_clipping_analysis();
  test_sha256_known_vectors();
  test_segmented_wav_store_is_self_describing_and_journaled();
  std::cout << "shipglows_audio_engine_tests: passed\n";
  return 0;
}
