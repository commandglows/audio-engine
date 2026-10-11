#include "shipglows/audio/visual_analysis.hpp"
#include "shipglows/audio/visual_analysis_wire.hpp"
#include "shipglows/audio/analysis_stream.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::vector<float> fixture(std::size_t frames) {
  std::vector<float> pcm(frames * 2);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto value = static_cast<float>(
        .25 * std::sin(2 * shipglows::audio::kVisualAnalysisPi * 997 *
                       static_cast<double>(frame) / 48000));
    pcm[frame * 2] = value;
    pcm[frame * 2 + 1] = -value;
  }
  return pcm;
}

std::unique_ptr<shipglows::audio::VisualAnalysisProcessor> process(
    unsigned mask, const std::vector<float>& pcm, std::size_t partition) {
  using shipglows::audio::VisualAnalysisProcessor;
  auto processor = std::make_unique<VisualAnalysisProcessor>(mask);
  const auto frames = pcm.size() / 2;
  for (std::size_t first = 0; first < frames; first += partition) {
    const auto count = std::min(partition, frames - first);
    processor->push(pcm.data() + first * 2, count, 96000 + first);
  }
  processor->finish();
  return processor;
}

void test_partition_and_profiles() {
  const auto pcm = fixture(2437);
  for (const unsigned mask : {3u, 5u, 11u, 13u}) {
    auto small = process(mask, pcm, 37);
    auto large = process(mask, pcm, 1000);
    const auto expected = (pcm.size() / 2 + 479) / 480;
    expect(small->history_count() == expected, "clean EOF emits each measured hop");
    expect(large->history_count() == expected, "alternate partition emits same hop count");
    expect(small->stats().fft_calls == ((mask & 8) ? expected * 2 : 0),
           "FFT only runs for spectrum masks");
    for (std::size_t index = 0; index < expected; ++index) {
      const auto& a = small->history(index);
      const auto& b = large->history(index);
      expect(a.sequence == index + 1 && a.center == 96000 + index * 480,
             "sequence and absolute center");
      expect(a.valid_frames == std::min<std::size_t>(480, 2437 - index * 480),
             "partial final hop frame count");
      expect(a.levels == b.levels && a.visual_levels == b.visual_levels,
             "measurement and visual transform are partition independent");
      expect(a.envelope == b.envelope && a.visual_envelope == b.visual_envelope &&
                 a.spectrum == b.spectrum && a.bands == b.bands,
             "optional features are partition independent");
      const auto encoded = shipglows::audio::encode_visual_analysis_record(a, mask);
      expect(encoded.size() == shipglows::audio::visual_analysis_record_bytes(mask),
             "fixed compact wire record length");
      expect(encoded[28] == ((mask & 2) ? 64 : 0) && encoded[32] == a.flags &&
                 encoded[36] == 0,
             "wire feature count, provenance flags and reserved field");
    }
  }
}

void test_discontinuity_and_queue() {
  using namespace shipglows::audio;
  auto processor = std::make_unique<VisualAnalysisProcessor>(5);
  const std::array<float, 960> block{};
  processor->push(block.data(), 480, 100);
  processor->push(block.data(), 480, 9000, true);
  processor->finish();
  expect(processor->history_count() == 1, "continuity reset clears old history");
  const auto& record = processor->history(0);
  expect(record.sequence == 2 && record.center == 9000 &&
             record.continuity == 2 && record.flags == 1,
         "discontinuity preserves global sequence and declares reset");

  AnalysisBlockQueue queue(480);
  std::array<float, 960> output{};
  AnalysisBlockQueue::Header header{};
  for (std::uint64_t index = 0; index < 16; ++index) {
    expect(queue.try_push(block.data(), 480, index * 480), "whole block admitted");
  }
  expect(!queue.try_push(block.data(), 480, 7680), "queue rejects a whole overflow block");
  for (std::uint64_t index = 0; index < 16; ++index) {
    expect(queue.try_pop(output.data(), header), "whole block dequeued");
    expect(header.position == index * 480 && header.frames == 480,
           "queue preserves source position and block boundary");
  }
  expect(!queue.try_pop(output.data(), header) && queue.drops() == 1,
         "queue reports overflow without partial blocks");
}

void test_analysis_stream_eof_and_cursor() {
  using namespace shipglows::audio;
  AnalysisStream stream(3, VisualAnalysisConfig{}, 480);
  expect(stream.bytes() <= 4 * 1024 * 1024,
         "stream allocation stays within the advertised native budget");
  const std::array<float, 960> block{};
  expect(stream.submit(block.data(), 480, 0, false, true), "stream admits submitted PCM");
  expect(stream.submit(block.data(), 480, 480, false, true), "stream admits second PCM block");
  stream.stop(true);
  const auto batch = stream.poll(0);
  expect(batch.state == "ended" && batch.has_origin && batch.origin_frame == 0,
         "clean stream stop drains queue and reports EOF");
  expect(batch.records.size() == 2 && batch.first_sequence == 1 &&
             batch.last_sequence == 2 && batch.next_sequence_cursor == 2,
         "stream returns committed records and cursor");
  bool rejected_future_cursor = false;
  try {
    (void)stream.poll(3);
  } catch (const std::invalid_argument&) {
    rejected_future_cursor = true;
  }
  expect(rejected_future_cursor, "stream rejects a cursor ahead of committed records");

  AnalysisStream positioned(5, VisualAnalysisConfig{}, 480);
  expect(positioned.submit(block.data(), 480, 24000, false, true),
         "stream admits a nonzero source origin");
  positioned.stop(true);
  const auto positioned_batch = positioned.poll(0);
  expect(positioned_batch.has_origin && positioned_batch.origin_frame == 24000 &&
             positioned_batch.records.size() == 1 &&
             positioned_batch.records.front().center == 24000,
         "stream preserves the adapter source frame domain");
}
}  // namespace

int main() {
  test_partition_and_profiles();
  test_discontinuity_and_queue();
  test_analysis_stream_eof_and_cursor();
}
