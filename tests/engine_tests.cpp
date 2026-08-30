#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "shipglows/audio/capture_session.hpp"
#include "shipglows/audio/segmented_pcm_store.hpp"
#include "shipglows/audio/spsc_audio_ring_buffer.hpp"
#include "shipglows/audio/pcm_analysis.hpp"

namespace {

using shipglows::audio::AudioFormat;
using shipglows::audio::CaptureSession;
using shipglows::audio::SampleFormat;
using shipglows::audio::SegmentedPcmStore;
using shipglows::audio::SessionState;
using shipglows::audio::SpscAudioRingBuffer;

void test_session_state_machine() {
  CaptureSession session;
  assert(!session.start());
  assert(session.prepare({48'000, 1, SampleFormat::int16}));
  assert(session.start());
  session.count_captured_frames(480);
  session.count_dropped_frames(2);
  session.count_discontinuity();
  assert(session.request_stop());
  assert(session.finish());
  assert(session.state() == SessionState::stopped);
  assert(session.metrics().frames_captured == 480);
  assert(session.metrics().frames_dropped == 2);
  assert(session.metrics().discontinuities == 1);
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

}  // namespace

int main() {
  test_session_state_machine();
  test_ring_buffer_wraps_without_overwrite();
  test_segment_rotation_and_recovery();
  test_clipping_analysis();
  std::cout << "shipglows_audio_engine_tests: passed\n";
  return 0;
}
