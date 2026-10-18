#include "shipglows/audio/synth_engine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <vector>

namespace {

std::atomic<bool> g_count_allocations{false};
std::atomic<std::uint64_t> g_allocations{0};

}  // namespace

void* operator new(std::size_t size) {
  if (g_count_allocations.load(std::memory_order_relaxed))
    g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {

using shipglows::audio::SynthEngine;
using shipglows::audio::SynthEvent;
using shipglows::audio::SynthEventQueue;
using shipglows::audio::SynthEventType;
using shipglows::audio::SynthInstrument;

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr std::uint32_t kBlock = 480;

std::uint32_t Frames(double seconds) {
  return static_cast<std::uint32_t>(seconds * SynthEngine::kSampleRate);
}

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

SynthEvent NoteOn(int midi, float velocity, SynthInstrument instrument) {
  SynthEvent event;
  event.type = SynthEventType::note_on;
  event.midi = static_cast<std::uint8_t>(midi);
  event.velocity = velocity;
  event.instrument = instrument;
  event.use_instrument = true;
  return event;
}

SynthEvent NoteOff(int midi) {
  SynthEvent event;
  event.type = SynthEventType::note_off;
  event.midi = static_cast<std::uint8_t>(midi);
  event.instrument = SynthInstrument::piano;
  event.use_instrument = true;
  return event;
}

SynthEvent Click(bool accent) {
  SynthEvent event;
  event.type = SynthEventType::click;
  event.accent = accent;
  return event;
}

// Renders `seconds` of mono output (left channel) in callback-sized blocks.
std::vector<float> Render(SynthEngine& engine, double seconds, std::uint32_t block = kBlock) {
  const auto frames = static_cast<std::uint32_t>(seconds * SynthEngine::kSampleRate);
  std::vector<float> stereo(static_cast<std::size_t>(block) * 2);
  std::vector<float> mono;
  mono.reserve(frames);
  for (std::uint32_t done = 0; done < frames; done += block) {
    const auto count = std::min(block, frames - done);
    std::fill(stereo.begin(), stereo.end(), 0.0F);
    engine.render_add(stereo.data(), count, 0, 0);
    for (std::uint32_t frame = 0; frame < count; ++frame) {
      expect(stereo[frame * 2] == stereo[frame * 2 + 1], "voices are mono on both channels");
      mono.push_back(stereo[frame * 2]);
    }
  }
  return mono;
}

float Peak(const std::vector<float>& samples, std::size_t first = 0,
           std::size_t last = SIZE_MAX) {
  float peak = 0.0F;
  last = std::min(last, samples.size());
  for (std::size_t i = first; i < last; ++i) {
    expect(std::isfinite(samples[i]), "samples are finite");
    peak = std::max(peak, std::abs(samples[i]));
  }
  return peak;
}

std::size_t ZeroCrossings(const std::vector<float>& samples, std::size_t first, std::size_t last) {
  std::size_t crossings = 0;
  for (std::size_t i = first + 1; i < last; ++i) {
    if ((samples[i - 1] < 0.0F) != (samples[i] < 0.0F)) ++crossings;
  }
  return crossings;
}

void QueueIsBoundedFifo() {
  SynthEventQueue queue;
  for (std::uint32_t round = 0; round < 3; ++round) {
    for (std::uint32_t i = 0; i < SynthEventQueue::kCapacity; ++i) {
      SynthEvent event;
      event.midi = static_cast<std::uint8_t>(i & 0x7f);
      event.post_ns = i + 1;
      expect(queue.try_push(event), "queue accepts up to capacity");
    }
    expect(!queue.try_push(SynthEvent{}), "full queue rejects");
    expect(queue.size() == SynthEventQueue::kCapacity, "queue size");
    for (std::uint32_t i = 0; i < SynthEventQueue::kCapacity; ++i) {
      SynthEvent event;
      expect(queue.try_pop(event), "queue pops");
      expect(event.post_ns == i + 1, "queue keeps FIFO order across wraparound");
    }
    SynthEvent event;
    expect(!queue.try_pop(event), "empty queue");
  }
}

void KickMatchesReferenceAndEnds() {
  SynthEngine engine;
  expect(engine.post(NoteOn(36, 1.0F, SynthInstrument::drums)), "post kick");
  const auto samples = Render(engine, 0.6);
  // Reference: the BeatGlows offline kick formula at 48 kHz.
  double phase = 0.0;
  for (std::size_t i = 0; i < 2000; ++i) {
    const double t = static_cast<double>(i) / 48000.0;
    phase += kTwoPi * (48 + 110 * std::exp(-t * 28)) / 48000.0;
    const double attack = t >= 0.002 ? 1.0 : t / 0.002;
    const double expected = std::sin(phase) * std::exp(-t * 7) * attack * 0.9;
    expect(std::abs(samples[i] - expected) < 1e-4, "kick follows the reference formula");
  }
  expect(Peak(samples) > 0.5F, "kick is audible");
  expect(Peak(samples, 21600) == 0.0F, "kick stops after 0.45 s");
  expect(engine.counters().active_voices == 0, "kick voice released");
}

void EveryDrumVoiceSounds() {
  for (const int midi : {36, 38, 39, 42, 44, 46}) {
    SynthEngine engine;
    expect(engine.post(NoteOn(midi, 0.8F, SynthInstrument::drums)), "post drum");
    const auto samples = Render(engine, 0.05);
    expect(Peak(samples) > 0.01F, "drum voice is audible");
    expect(engine.counters().ignored == 0, "GM drum voice is known");
  }
  SynthEngine engine;
  expect(engine.post(NoteOn(60, 0.8F, SynthInstrument::drums)), "post unknown drum");
  const auto samples = Render(engine, 0.05);
  expect(Peak(samples) == 0.0F, "unknown drum note is silent");
  expect(engine.counters().ignored == 1, "unknown drum note is counted");
}

void ClosedHatChokesOpenHat() {
  SynthEngine ringing;
  expect(ringing.post(NoteOn(46, 1.0F, SynthInstrument::drums)), "post open hat");
  const auto open = Render(ringing, 0.3);

  SynthEngine choked;
  expect(choked.post(NoteOn(46, 1.0F, SynthInstrument::drums)), "post open hat");
  auto first = Render(choked, 0.1);
  expect(choked.post(NoteOn(42, 1.0F, SynthInstrument::drums)), "post closed hat");
  const auto rest = Render(choked, 0.2);
  // Open hat still rings at 0.2 s alone; once choked only the 70 ms closed
  // hat remains, so the tail from 0.2 s on is silent.
  expect(Peak(open, 9600, 14400) > 0.01F, "open hat rings");
  expect(Peak(rest, 4800, 9600) == 0.0F, "closed hat chokes the open hat");
  expect(Peak(rest, 0, 144 * 2) > 0.0F, "choke fades instead of cutting");
}

void PianoSustainsAndReleases() {
  SynthEngine engine;
  expect(engine.post(NoteOn(69, 1.0F, SynthInstrument::piano)), "post piano");
  const auto held = Render(engine, 2.0);
  expect(Peak(held, 0, 4800) > 0.2F, "piano onset is audible");
  // Sustain level: 0.35 (FM peak 1) * 0.35 * velocity.
  const float sustain = Peak(held, 86400, 96000);
  expect(sustain > 0.10F && sustain < 0.16F, "piano sustains while held");
  const auto crossings = ZeroCrossings(held, 86400, 96000);
  expect(crossings > 40 && crossings < 200, "piano keeps a pitched carrier");
  expect(engine.post(NoteOff(69)), "post note off");
  const auto released = Render(engine, 0.5);
  expect(Peak(released, 0, 480) > 0.05F, "release is gradual");
  expect(Peak(released, Frames(0.31)) == 0.0F, "piano is silent after release");
  expect(engine.counters().active_voices == 0, "piano voice freed");
}

void VelocityZeroIsNoteOff() {
  SynthEngine engine;
  expect(engine.post(NoteOn(60, 0.9F, SynthInstrument::piano)), "post piano");
  (void)Render(engine, 0.1);
  expect(engine.post(NoteOn(60, 0.0F, SynthInstrument::piano)), "post zero velocity");
  const auto tail = Render(engine, 0.5);
  expect(Peak(tail, Frames(0.31)) == 0.0F, "velocity zero releases the key");
}

void ClickAccentIsDistinct() {
  SynthEngine plain;
  expect(plain.post(Click(false)), "post click");
  const auto normal = Render(plain, 0.04);
  SynthEngine accented;
  expect(accented.post(Click(true)), "post accent");
  const auto accent = Render(accented, 0.04);
  expect(Peak(accent) > Peak(normal) * 1.3F, "accent is louder");
  const auto normal_crossings = ZeroCrossings(normal, 0, 1440);
  const auto accent_crossings = ZeroCrossings(accent, 0, 1440);
  expect(normal_crossings >= 55 && normal_crossings <= 62, "plain click is 1 kHz");
  expect(accent_crossings >= 85 && accent_crossings <= 92, "accent click is 1.5 kHz");
  expect(Peak(normal, 1440) == 0.0F, "click lasts 30 ms");
}

void CurrentInstrumentAppliesToNotes() {
  SynthEngine engine;
  SynthEvent set;
  set.type = SynthEventType::set_instrument;
  set.instrument = SynthInstrument::drums;
  expect(engine.post(set), "post set voice");
  SynthEvent kick;
  kick.type = SynthEventType::note_on;
  kick.midi = 36;
  kick.velocity = 1.0F;
  expect(engine.post(kick), "post default-instrument note");
  const auto samples = Render(engine, 0.6);
  expect(engine.instrument() == SynthInstrument::drums, "instrument switched");
  expect(Peak(samples, 21600) == 0.0F, "note used the drum kit (one-shot)");
}

void PolyphonyIsBounded() {
  SynthEngine engine;
  for (int i = 0; i < 64; ++i) {
    expect(engine.post(NoteOn(40 + (i % 40), 1.0F, SynthInstrument::piano)), "post chord note");
  }
  const auto samples = Render(engine, 0.2);
  const auto counters = engine.counters();
  expect(counters.peak_active_voices <= SynthEngine::kVoiceSlots, "voice pool is bounded");
  expect(counters.active_voices <= SynthEngine::kPolyphony, "stolen voices faded out");
  expect(counters.stolen_voices >= 32, "extra notes steal voices");
  expect(Peak(samples) < 64.0F, "sum stays finite and bounded");
}

void AllNotesOffFades() {
  SynthEngine engine;
  for (int midi : {60, 64, 67}) expect(engine.post(NoteOn(midi, 1.0F, SynthInstrument::piano)), "post");
  (void)Render(engine, 0.1);
  SynthEvent off;
  off.type = SynthEventType::all_notes_off;
  expect(engine.post(off), "post all off");
  const auto tail = Render(engine, 0.02);
  expect(Peak(tail, 0, 48) > 0.0F, "all-off fades");
  expect(Peak(tail, 240) == 0.0F, "all-off silences within 5 ms");
  expect(engine.counters().active_voices == 0, "all voices freed");
}

void RenderAddsWithoutAllocating() {
  SynthEngine engine;
  std::vector<float> stereo(1024 * 2, 0.25F);
  for (int i = 0; i < 48; ++i) {
    expect(engine.post(NoteOn(i % 2 ? 46 : 38, 1.0F, SynthInstrument::drums)), "post");
    expect(engine.post(NoteOn(48 + i, 0.7F, SynthInstrument::piano)), "post");
  }
  expect(engine.post(Click(true)), "post click");
  g_allocations.store(0);
  g_count_allocations.store(true);
  for (int block = 0; block < 200; ++block) engine.render_add(stereo.data(), 1024, 0, 0);
  g_count_allocations.store(false);
  expect(g_allocations.load() == 0, "render_add never allocates");

  SynthEngine silent;
  std::vector<float> base(64 * 2, 0.25F);
  silent.render_add(base.data(), 64, 0, 0);
  expect(std::all_of(base.begin(), base.end(), [](float v) { return v == 0.25F; }),
         "render_add mixes onto existing content");
}

void ResetDiscardsQueuedEventsAndVoices() {
  SynthEngine engine;
  expect(engine.post(NoteOn(60, 1.0F, SynthInstrument::piano)), "post held key");
  (void)Render(engine, 0.05);
  for (int i = 0; i < 8; ++i) expect(engine.post(NoteOn(36, 1.0F, SynthInstrument::drums)), "post");
  engine.reset();
  const auto samples = Render(engine, 0.1);
  expect(Peak(samples) == 0.0F, "reset silences voices and drops queued notes");
  expect(engine.counters().active_voices == 0, "no voice survives reset");
  expect(engine.counters().applied == 1, "queued events were not applied");
}

void LatencyIsMeasuredFromPostTime() {
  SynthEngine engine;
  auto event = NoteOn(36, 1.0F, SynthInstrument::drums);
  event.post_ns = 1'000'000;
  expect(engine.post(event), "post stamped event");
  std::vector<float> stereo(kBlock * 2, 0.0F);
  engine.render_add(stereo.data(), kBlock, 3'000'000, 21'000'000);
  const auto queue = engine.queue_latency();
  const auto output = engine.output_latency();
  expect(queue.count == 1 && queue.max_us == 2000, "queue latency is block start minus post");
  expect(output.count == 1 && output.max_us == 20000, "output latency reaches presentation");
  expect(output.p50_us == 20250 && output.p99_us == 20250, "percentile uses bucket upper bound");
  engine.clear_latency();
  expect(engine.output_latency().count == 0, "latency can be cleared");
}

}  // namespace

int main() {
  QueueIsBoundedFifo();
  KickMatchesReferenceAndEnds();
  EveryDrumVoiceSounds();
  ClosedHatChokesOpenHat();
  PianoSustainsAndReleases();
  VelocityZeroIsNoteOff();
  ClickAccentIsDistinct();
  CurrentInstrumentAppliesToNotes();
  PolyphonyIsBounded();
  AllNotesOffFades();
  RenderAddsWithoutAllocating();
  LatencyIsMeasuredFromPostTime();
  ResetDiscardsQueuedEventsAndVoices();
  std::cout << "synth engine tests passed\n";
  return 0;
}
