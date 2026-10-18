#include "shipglows/audio/synth_engine.hpp"

#include <algorithm>
#include <cmath>

namespace shipglows::audio {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kRate = SynthEngine::kSampleRate;
constexpr float kChokeFadeSeconds = 0.003F;
constexpr float kAllOffFadeSeconds = 0.005F;
constexpr float kPianoReleaseSeconds = 0.3F;
// A key held longer than this is released, so a lost note-off cannot keep a
// voice sounding forever.
constexpr std::uint32_t kMaxHeldFrames = SynthEngine::kSampleRate * 30;

constexpr std::uint8_t kBassDrum = 36;
constexpr std::uint8_t kSnare = 38;
constexpr std::uint8_t kClap = 39;
constexpr std::uint8_t kClosedHat = 42;
constexpr std::uint8_t kPedalHat = 44;
constexpr std::uint8_t kOpenHat = 46;

constexpr std::uint32_t Frames(double seconds) noexcept {
  return static_cast<std::uint32_t>(seconds * kRate + 0.5);
}

inline double DecayStep(double rate_per_second) noexcept {
  return std::exp(-rate_per_second / kRate);
}

inline float Attack(std::uint32_t frame, std::uint32_t attack_frames) noexcept {
  return frame >= attack_frames ? 1.0F
                                : static_cast<float>(frame) / static_cast<float>(attack_frames);
}

inline std::uint64_t ElapsedMicroseconds(std::uint64_t from_ns, std::uint64_t to_ns) noexcept {
  return to_ns > from_ns ? (to_ns - from_ns) / 1000 : 0;
}

}  // namespace

bool SynthEventQueue::try_push(const SynthEvent& event) noexcept {
  const auto head = head_.load(std::memory_order_relaxed);
  const auto tail = tail_.load(std::memory_order_acquire);
  if (head - tail >= kCapacity) return false;
  slots_[head & (kCapacity - 1)] = event;
  head_.store(head + 1, std::memory_order_release);
  return true;
}

bool SynthEventQueue::try_pop(SynthEvent& event) noexcept {
  const auto tail = tail_.load(std::memory_order_relaxed);
  const auto head = head_.load(std::memory_order_acquire);
  if (tail == head) return false;
  event = slots_[tail & (kCapacity - 1)];
  tail_.store(tail + 1, std::memory_order_release);
  return true;
}

std::uint32_t SynthEventQueue::size() const noexcept {
  return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
}

void SynthLatencyHistogram::record(std::uint64_t microseconds) noexcept {
  const auto bucket = std::min<std::uint64_t>(microseconds / kBucketMicroseconds, kBuckets - 1);
  buckets_[static_cast<std::size_t>(bucket)].fetch_add(1, std::memory_order_relaxed);
  // Single writer: plain load/store pairs keep the callback free of CAS loops.
  sum_us_.store(sum_us_.load(std::memory_order_relaxed) + microseconds, std::memory_order_relaxed);
  if (microseconds < min_us_.load(std::memory_order_relaxed))
    min_us_.store(microseconds, std::memory_order_relaxed);
  if (microseconds > max_us_.load(std::memory_order_relaxed))
    max_us_.store(microseconds, std::memory_order_relaxed);
  count_.fetch_add(1, std::memory_order_release);
}

SynthLatencyHistogram::Summary SynthLatencyHistogram::summary() const noexcept {
  Summary result;
  result.count = count_.load(std::memory_order_acquire);
  if (result.count == 0) return result;
  result.min_us = min_us_.load(std::memory_order_relaxed);
  result.max_us = max_us_.load(std::memory_order_relaxed);
  result.mean_us = sum_us_.load(std::memory_order_relaxed) / result.count;
  std::uint64_t total = 0;
  for (const auto& bucket : buckets_) total += bucket.load(std::memory_order_relaxed);
  const auto percentile = [&](std::uint64_t numerator) {
    const auto target = (total * numerator + 99) / 100;
    std::uint64_t cumulative = 0;
    for (std::size_t i = 0; i < kBuckets; ++i) {
      cumulative += buckets_[i].load(std::memory_order_relaxed);
      if (cumulative >= target && cumulative != 0) {
        return i + 1 == kBuckets ? result.max_us
                                 : static_cast<std::uint64_t>(i + 1) * kBucketMicroseconds;
      }
    }
    return result.max_us;
  };
  result.p50_us = percentile(50);
  result.p95_us = percentile(95);
  result.p99_us = percentile(99);
  return result;
}

void SynthLatencyHistogram::clear() noexcept {
  for (auto& bucket : buckets_) bucket.store(0, std::memory_order_relaxed);
  sum_us_.store(0, std::memory_order_relaxed);
  min_us_.store(UINT64_MAX, std::memory_order_relaxed);
  max_us_.store(0, std::memory_order_relaxed);
  count_.store(0, std::memory_order_release);
}

SynthEngine::SynthEngine() noexcept = default;

bool SynthEngine::post(const SynthEvent& event) noexcept {
  if (!queue_.try_push(event)) {
    rejected_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  posted_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void SynthEngine::render_add(float* interleaved_stereo, std::uint32_t frames,
                             std::uint64_t now_ns, std::uint64_t presentation_ns) noexcept {
  SynthEvent event;
  // Bounded by the queue capacity: events posted during this loop wait for
  // the next block.
  for (std::uint32_t budget = SynthEventQueue::kCapacity; budget != 0 && queue_.try_pop(event); --budget) {
    apply(event, now_ns, presentation_ns);
  }
  if (!interleaved_stereo) frames = 0;
  std::uint32_t active = 0;
  for (auto& voice : voices_) {
    if (!voice.active) continue;
    for (std::uint32_t frame = 0; frame < frames && voice.active; ++frame) {
      const float sample = next_sample(voice);
      interleaved_stereo[static_cast<std::size_t>(frame) * 2] += sample;
      interleaved_stereo[static_cast<std::size_t>(frame) * 2 + 1] += sample;
    }
    if (voice.active) ++active;
  }
  active_.store(active, std::memory_order_relaxed);
}

SynthCounters SynthEngine::counters() const noexcept {
  SynthCounters counters;
  counters.posted = posted_.load(std::memory_order_relaxed);
  counters.rejected = rejected_.load(std::memory_order_relaxed);
  counters.applied = applied_.load(std::memory_order_relaxed);
  counters.ignored = ignored_.load(std::memory_order_relaxed);
  counters.stolen_voices = stolen_.load(std::memory_order_relaxed);
  counters.active_voices = active_.load(std::memory_order_relaxed);
  counters.peak_active_voices = peak_active_.load(std::memory_order_relaxed);
  return counters;
}

SynthInstrument SynthEngine::instrument() const noexcept {
  return instrument_.load(std::memory_order_relaxed);
}

SynthLatencyHistogram::Summary SynthEngine::queue_latency() const noexcept {
  return queue_latency_.summary();
}

SynthLatencyHistogram::Summary SynthEngine::output_latency() const noexcept {
  return output_latency_.summary();
}

void SynthEngine::clear_latency() noexcept {
  queue_latency_.clear();
  output_latency_.clear();
}

void SynthEngine::apply(const SynthEvent& event, std::uint64_t now_ns,
                        std::uint64_t presentation_ns) noexcept {
  applied_.fetch_add(1, std::memory_order_relaxed);
  const auto instrument = event.use_instrument ? event.instrument : instrument_audio_;
  bool sounding = false;
  switch (event.type) {
    case SynthEventType::note_on:
      if (event.midi > 127 || !std::isfinite(event.velocity)) {
        ignored_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      if (event.velocity <= 0.0F) {
        note_off(instrument, event.midi);  // MIDI convention: velocity 0 is note-off.
        return;
      }
      note_on(instrument, event.midi, std::min(event.velocity, 1.0F));
      sounding = instrument == SynthInstrument::piano ||
                 event.midi == kBassDrum || event.midi == kSnare || event.midi == kClap ||
                 event.midi == kClosedHat || event.midi == kPedalHat || event.midi == kOpenHat;
      break;
    case SynthEventType::note_off:
      note_off(instrument, event.midi);
      break;
    case SynthEventType::click:
      start_voice(Kind::click, 0, 1.0F, event.accent);
      sounding = true;
      break;
    case SynthEventType::set_instrument:
      if (event.instrument != SynthInstrument::piano && event.instrument != SynthInstrument::drums) {
        ignored_.fetch_add(1, std::memory_order_relaxed);
        return;
      }
      instrument_audio_ = event.instrument;
      instrument_.store(event.instrument, std::memory_order_relaxed);
      break;
    case SynthEventType::all_notes_off:
      for (auto& voice : voices_) {
        if (voice.active) fade(voice, kAllOffFadeSeconds);
      }
      break;
  }
  if (event.post_ns == 0) return;
  queue_latency_.record(ElapsedMicroseconds(event.post_ns, now_ns));
  if (sounding && presentation_ns != 0)
    output_latency_.record(ElapsedMicroseconds(event.post_ns, presentation_ns));
}

void SynthEngine::note_on(SynthInstrument instrument, std::uint8_t midi, float velocity) noexcept {
  if (instrument == SynthInstrument::piano) {
    // A repeated key releases its previous strike instead of stacking voices.
    for (auto& voice : voices_) {
      if (voice.active && voice.kind == Kind::piano && voice.midi == midi && voice.held) release(voice);
    }
    start_voice(Kind::piano, midi, velocity, false);
    return;
  }
  switch (midi) {
    case kBassDrum: start_voice(Kind::kick, midi, velocity, false); return;
    case kSnare: start_voice(Kind::snare, midi, velocity, false); return;
    case kClap: start_voice(Kind::clap, midi, velocity, false); return;
    case kClosedHat:
    case kPedalHat:
    case kOpenHat:
      // One hi-hat: any new hat strike chokes a ringing open hat.
      for (auto& voice : voices_) {
        if (voice.active && voice.kind == Kind::open_hat && voice.fade_step == 0.0F)
          fade(voice, kChokeFadeSeconds);
      }
      start_voice(midi == kOpenHat ? Kind::open_hat : Kind::closed_hat, midi, velocity, false);
      return;
    default:
      ignored_.fetch_add(1, std::memory_order_relaxed);
      return;
  }
}

void SynthEngine::note_off(SynthInstrument instrument, std::uint8_t midi) noexcept {
  if (instrument != SynthInstrument::piano) return;  // Drum voices ring out.
  for (auto& voice : voices_) {
    if (voice.active && voice.kind == Kind::piano && voice.midi == midi && voice.held) release(voice);
  }
}

SynthEngine::Voice* SynthEngine::allocate_voice() noexcept {
  std::uint32_t sounding = 0;
  Voice* oldest = nullptr;
  for (auto& voice : voices_) {
    if (!voice.active || voice.fade_step > 0.0F) continue;
    ++sounding;
    // Prefer stealing a released key or a one-shot voice over a held key.
    const bool candidate_held = voice.kind == Kind::piano && voice.held;
    if (!oldest) {
      oldest = &voice;
      continue;
    }
    const bool oldest_held = oldest->kind == Kind::piano && oldest->held;
    if ((oldest_held && !candidate_held) ||
        (oldest_held == candidate_held && voice.serial < oldest->serial)) oldest = &voice;
  }
  if (sounding >= kPolyphony && oldest) {
    fade(*oldest, kChokeFadeSeconds);
    stolen_.fetch_add(1, std::memory_order_relaxed);
  }
  for (auto& voice : voices_) {
    if (!voice.active) return &voice;
  }
  // Every slot is busy, including the fade-out reserve: take the quietest.
  Voice* quietest = &voices_[0];
  for (auto& voice : voices_) {
    if (voice.fade_gain < quietest->fade_gain ||
        (voice.fade_gain == quietest->fade_gain && voice.serial < quietest->serial)) quietest = &voice;
  }
  stolen_.fetch_add(1, std::memory_order_relaxed);
  return quietest;
}

void SynthEngine::start_voice(Kind kind, std::uint8_t midi, float velocity, bool accent) noexcept {
  Voice* slot = allocate_voice();
  Voice voice;
  voice.active = true;
  voice.kind = kind;
  voice.midi = midi;
  voice.velocity = velocity;
  voice.serial = next_serial_++;
  noise_seed_ += 0x9e3779b9u;
  voice.noise = noise_seed_ == 0 ? 0x9e3779b9u : noise_seed_;
  switch (kind) {
    case Kind::kick:
      voice.length = Frames(0.45);
      voice.decay_a_step = DecayStep(7);
      voice.decay_b_step = DecayStep(28);
      break;
    case Kind::snare:
      voice.length = Frames(0.25);
      voice.phase_increment = kTwoPi * 185.0 / kRate;
      voice.decay_a_step = DecayStep(28);
      voice.decay_b_step = DecayStep(16);
      break;
    case Kind::closed_hat:
      voice.length = Frames(0.07);
      voice.decay_a_step = DecayStep(70);
      break;
    case Kind::open_hat:
      voice.length = Frames(0.45);
      voice.decay_a_step = DecayStep(7);
      break;
    case Kind::clap:
      voice.length = Frames(0.22);
      voice.decay_a_step = DecayStep(18);
      voice.decay_b_step = DecayStep(900);
      break;
    case Kind::click:
      voice.length = Frames(0.03);
      voice.phase_increment = kTwoPi * (accent ? 1500.0 : 1000.0) / kRate;
      voice.velocity = accent ? 0.7F : 0.45F;
      voice.decay_a_step = DecayStep(140);
      break;
    case Kind::piano: {
      const double frequency = 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
      voice.held = true;
      voice.phase_increment = kTwoPi * frequency / kRate;
      voice.modulator_increment = 2.0 * voice.phase_increment;
      voice.decay_a_step = DecayStep(2.5);  // modulation index
      voice.decay_b_step = DecayStep(2.0);  // level decay towards sustain
      break;
    }
  }
  *slot = voice;
  std::uint32_t sounding = 0;
  for (const auto& candidate : voices_) {
    if (candidate.active) ++sounding;
  }
  if (sounding > peak_active_.load(std::memory_order_relaxed))
    peak_active_.store(sounding, std::memory_order_relaxed);
}

void SynthEngine::fade(Voice& voice, float seconds) noexcept {
  const float step = 1.0F / std::max(1.0F, seconds * static_cast<float>(kRate));
  if (voice.fade_step == 0.0F || step > voice.fade_step) voice.fade_step = step;
}

void SynthEngine::release(Voice& voice) noexcept {
  voice.held = false;
  voice.released = true;
  voice.release_step = 1.0F / (kPianoReleaseSeconds * static_cast<float>(kRate));
}

float SynthEngine::noise(Voice& voice) noexcept {
  auto x = voice.noise;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  voice.noise = x;
  return static_cast<float>(static_cast<double>(x) / 2147483648.0 - 1.0);
}

float SynthEngine::next_sample(Voice& voice) noexcept {
  const std::uint32_t frame = voice.frame;
  double value = 0.0;
  switch (voice.kind) {
    case Kind::kick: {
      voice.phase += kTwoPi * (48.0 + 110.0 * voice.decay_b) / kRate;
      value = std::sin(voice.phase) * voice.decay_a * Attack(frame, 96) * voice.velocity * 0.9;
      break;
    }
    case Kind::snare: {
      const float raw = noise(voice);
      const double bright = (raw - voice.previous_noise) * 0.5;
      voice.previous_noise = raw;
      const double body = std::sin(voice.phase) * voice.decay_a * 0.5;
      voice.phase += voice.phase_increment;
      value = (body + bright * voice.decay_b) * Attack(frame, 48) * voice.velocity;
      break;
    }
    case Kind::closed_hat:
    case Kind::open_hat: {
      const float raw = noise(voice);
      const double bright = raw - voice.previous_noise;
      voice.previous_noise = raw;
      const double level = voice.kind == Kind::closed_hat ? 0.35 : 0.3;
      value = bright * voice.decay_a * level * voice.velocity * Attack(frame, 48);
      break;
    }
    case Kind::clap: {
      const float raw = noise(voice);
      const double bright = raw - voice.previous_noise;
      voice.previous_noise = raw;
      static constexpr std::uint32_t kBurstStarts[3] = {0, Frames(0.008), Frames(0.016)};
      double envelope = voice.decay_a;
      for (std::size_t burst = 0; burst < 3; ++burst) {
        if (frame == kBurstStarts[burst]) voice.burst[burst] = 1.0;
        if (frame >= kBurstStarts[burst]) {
          envelope += voice.burst[burst];
          voice.burst[burst] *= voice.decay_b_step;
        }
      }
      value = bright * envelope * 0.45 * voice.velocity;
      break;
    }
    case Kind::click: {
      value = std::sin(voice.phase) * voice.decay_a * voice.velocity;
      voice.phase += voice.phase_increment;
      break;
    }
    case Kind::piano: {
      const double index = 2.2 * voice.decay_a + 0.25;
      const double modulator = std::sin(voice.modulator_phase);
      const double carrier = std::sin(voice.phase + index * modulator);
      voice.phase += voice.phase_increment;
      voice.modulator_phase += voice.modulator_increment;
      if (voice.phase > kTwoPi) voice.phase -= kTwoPi;
      if (voice.modulator_phase > kTwoPi) voice.modulator_phase -= kTwoPi;
      const double level = 0.35 + 0.65 * voice.decay_b;
      value = carrier * level * voice.release_gain * Attack(frame, 192) * voice.velocity * 0.35;
      if (voice.held && ++voice.held_frames >= kMaxHeldFrames) release(voice);
      if (voice.released) {
        voice.release_gain -= voice.release_step;
        if (voice.release_gain <= voice.release_step * 0.5F) voice.active = false;
      }
      break;
    }
  }
  voice.decay_a *= voice.decay_a_step;
  voice.decay_b *= voice.decay_b_step;
  value *= voice.fade_gain;
  if (voice.fade_step > 0.0F) {
    voice.fade_gain -= voice.fade_step;
    if (voice.fade_gain <= voice.fade_step * 0.5F) voice.active = false;
  }
  ++voice.frame;
  if (voice.kind != Kind::piano && voice.frame >= voice.length) voice.active = false;
  return static_cast<float>(value);
}

}  // namespace shipglows::audio
