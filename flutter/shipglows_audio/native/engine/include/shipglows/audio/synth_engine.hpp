#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace shipglows::audio {

// Melodic target of a note event. Clicks are a separate event type.
enum class SynthInstrument : std::uint8_t { piano = 0, drums = 1 };

enum class SynthEventType : std::uint8_t {
  note_on = 0,
  note_off = 1,
  click = 2,
  set_instrument = 3,
  all_notes_off = 4,
};

// Fixed-size control event. `post_ns` is the steady-clock time at which the
// control thread posted it; it only feeds latency statistics.
struct SynthEvent final {
  SynthEventType type = SynthEventType::note_on;
  SynthInstrument instrument = SynthInstrument::piano;
  bool use_instrument = false;  // false: the engine's current instrument.
  bool accent = false;
  std::uint8_t midi = 0;
  float velocity = 0.0F;
  std::uint64_t post_ns = 0;
};

// Preallocated single-producer/single-consumer event queue. The producer is
// one control thread; the consumer is the audio callback. Neither side
// allocates, locks or waits; a full queue rejects the new event.
class SynthEventQueue final {
 public:
  static constexpr std::uint32_t kCapacity = 256;
  static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity is a power of two");
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "synth event indices must be lock-free");

  [[nodiscard]] bool try_push(const SynthEvent& event) noexcept;
  [[nodiscard]] bool try_pop(SynthEvent& event) noexcept;
  [[nodiscard]] std::uint32_t size() const noexcept;

 private:
  std::array<SynthEvent, kCapacity> slots_{};
  alignas(64) std::atomic<std::uint32_t> head_{0};  // producer-owned
  alignas(64) std::atomic<std::uint32_t> tail_{0};  // consumer-owned
};

// Latency histogram written only by the audio thread with relaxed atomics and
// sampled by the control thread. Buckets are 250 us wide up to 100 ms; the
// last bucket also holds every larger value.
class SynthLatencyHistogram final {
 public:
  static constexpr std::uint32_t kBucketMicroseconds = 250;
  static constexpr std::size_t kBuckets = 401;

  struct Summary final {
    std::uint64_t count = 0;
    std::uint64_t min_us = 0;
    std::uint64_t max_us = 0;
    std::uint64_t mean_us = 0;
    // Upper bounds of the bucket holding each percentile.
    std::uint64_t p50_us = 0;
    std::uint64_t p95_us = 0;
    std::uint64_t p99_us = 0;
  };

  void record(std::uint64_t microseconds) noexcept;
  [[nodiscard]] Summary summary() const noexcept;
  // Control side; only meaningful while the audio thread is not recording.
  void clear() noexcept;

 private:
  std::array<std::atomic<std::uint64_t>, kBuckets> buckets_{};
  std::atomic<std::uint64_t> count_{0};
  std::atomic<std::uint64_t> sum_us_{0};
  std::atomic<std::uint64_t> min_us_{UINT64_MAX};
  std::atomic<std::uint64_t> max_us_{0};
};

struct SynthCounters final {
  std::uint64_t posted = 0;
  std::uint64_t rejected = 0;  // queue full
  std::uint64_t applied = 0;
  std::uint64_t ignored = 0;   // unknown drum note, invalid values
  std::uint64_t stolen_voices = 0;
  std::uint32_t active_voices = 0;
  std::uint32_t peak_active_voices = 0;
};

// Real-time polyphonic synthesizer: General MIDI drum voices (36, 38, 39, 42,
// 44, 46 with hi-hat choke), an FM piano and an accented metronome click. All
// voices are mono and added equally to both stereo channels; the caller owns
// the master limiter. The timbres follow the BeatGlows offline renderer.
//
// Threading: post() belongs to one control thread; render_add() to one audio
// thread. render_add() never allocates, locks, waits or touches files.
class SynthEngine final {
 public:
  static constexpr std::uint32_t kSampleRate = 48'000;
  // Notes sounding at once; extra slots let stolen voices fade out.
  static constexpr std::uint32_t kPolyphony = 32;
  static constexpr std::uint32_t kVoiceSlots = 40;

  SynthEngine() noexcept;
  SynthEngine(const SynthEngine&) = delete;
  SynthEngine& operator=(const SynthEngine&) = delete;

  // Control thread. Stamps nothing: the caller sets post_ns.
  [[nodiscard]] bool post(const SynthEvent& event) noexcept;

  // Audio thread. Applies queued events at the block start, then adds the
  // voices to `interleaved_stereo`. `now_ns` is the steady-clock time of the
  // block start; `presentation_ns`, when nonzero, is the estimated time at
  // which the first frame of the block reaches the endpoint (same clock).
  void render_add(float* interleaved_stereo, std::uint32_t frames,
                  std::uint64_t now_ns, std::uint64_t presentation_ns) noexcept;

  [[nodiscard]] SynthCounters counters() const noexcept;
  [[nodiscard]] SynthInstrument instrument() const noexcept;
  // Time from post() to the start of the block that applied the event.
  [[nodiscard]] SynthLatencyHistogram::Summary queue_latency() const noexcept;
  // Time from post() to the estimated endpoint presentation of the onset.
  [[nodiscard]] SynthLatencyHistogram::Summary output_latency() const noexcept;
  void clear_latency() noexcept;

 private:
  enum class Kind : std::uint8_t { kick, snare, closed_hat, open_hat, clap, click, piano };
  struct Voice final {
    bool active = false;
    Kind kind = Kind::kick;
    std::uint8_t midi = 0;
    bool held = false;      // piano key down
    bool released = false;  // piano release started
    std::uint64_t serial = 0;
    std::uint32_t frame = 0;
    std::uint32_t length = 0;      // natural end for one-shot voices
    std::uint32_t held_frames = 0;
    float velocity = 0.0F;
    float release_gain = 1.0F;
    float release_step = 0.0F;
    float fade_gain = 1.0F;
    float fade_step = 0.0F;        // > 0 while choked or stolen
    double phase = 0.0;
    double phase_increment = 0.0;
    double modulator_phase = 0.0;
    double modulator_increment = 0.0;
    double decay_a = 1.0, decay_a_step = 1.0;
    double decay_b = 1.0, decay_b_step = 1.0;
    std::array<double, 3> burst{};
    std::uint32_t noise = 1;
    float previous_noise = 0.0F;
  };

  void apply(const SynthEvent& event, std::uint64_t now_ns,
             std::uint64_t presentation_ns) noexcept;
  void note_on(SynthInstrument instrument, std::uint8_t midi, float velocity) noexcept;
  void note_off(SynthInstrument instrument, std::uint8_t midi) noexcept;
  void start_voice(Kind kind, std::uint8_t midi, float velocity, bool accent) noexcept;
  Voice* allocate_voice() noexcept;
  void fade(Voice& voice, float seconds) noexcept;
  void release(Voice& voice) noexcept;
  [[nodiscard]] float next_sample(Voice& voice) noexcept;
  [[nodiscard]] float noise(Voice& voice) noexcept;

  SynthEventQueue queue_;
  // Audio-thread state.
  std::array<Voice, kVoiceSlots> voices_{};
  std::uint64_t next_serial_ = 1;
  std::uint32_t noise_seed_ = 0x9e3779b9u;
  SynthInstrument instrument_audio_ = SynthInstrument::piano;
  // Shared counters, single writer each.
  std::atomic<SynthInstrument> instrument_{SynthInstrument::piano};
  std::atomic<std::uint64_t> posted_{0};
  std::atomic<std::uint64_t> rejected_{0};
  std::atomic<std::uint64_t> applied_{0};
  std::atomic<std::uint64_t> ignored_{0};
  std::atomic<std::uint64_t> stolen_{0};
  std::atomic<std::uint32_t> active_{0};
  std::atomic<std::uint32_t> peak_active_{0};
  SynthLatencyHistogram queue_latency_;
  SynthLatencyHistogram output_latency_;
};

}  // namespace shipglows::audio
