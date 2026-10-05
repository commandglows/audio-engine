#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace shipglows::audio {

struct DspParameters final {
  bool eq_enabled = false;
  float low_gain_db = 0.0F;
  float mid_gain_db = 0.0F;
  float high_gain_db = 0.0F;
  bool gate_enabled = false;
  float gate_threshold_dbfs = -60.0F;
  float gate_release_ms = 120.0F;
  bool compressor_enabled = false;
  float compressor_threshold_dbfs = -18.0F;
  float compressor_ratio = 4.0F;
  float compressor_attack_ms = 10.0F;
  float compressor_release_ms = 100.0F;
};

// Stateful, allocation-free stereo float processor. set_parameters() is for one
// control producer; process() is for one serialized audio consumer. Updates are
// queued without blocking and become active at a process() block boundary.
class DspChain final {
 public:
  explicit DspChain(std::uint32_t sample_rate = 48'000) noexcept;

  [[nodiscard]] bool set_parameters(const DspParameters& parameters) noexcept;
  // reset() is audio-consumer-only and must not overlap process().
  void reset() noexcept;

  // Samples are interleaved stereo. Invalid pointers/sizes are ignored.
  void process(float* interleaved_stereo, std::size_t frame_count) noexcept;

 private:
  struct Biquad final {
    float b0 = 1.0F, b1 = 0.0F, b2 = 0.0F, a1 = 0.0F, a2 = 0.0F;
    float z1 = 0.0F, z2 = 0.0F;
    float run(float input) noexcept;
  };
  struct Update final {
    DspParameters parameters{};
    Biquad low[2]{}, mid[2]{}, high[2]{};
  };
  static constexpr std::uint32_t kUpdateQueueCapacity = 4;
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                "DSP update indices must be lock-free on target platforms");
  void apply_pending_updates() noexcept;
  std::uint32_t sample_rate_;
  // parameters_ and filters are consumer-owned; slots are producer-owned until
  // head_ publication and consumer-owned until tail_ publication.
  DspParameters parameters_{};
  Biquad low_[2]{}, mid_[2]{}, high_[2]{};
  Update updates_[kUpdateQueueCapacity]{};
  std::atomic<std::uint32_t> update_head_{0};
  std::atomic<std::uint32_t> update_tail_{0};
  float gate_envelope_[2]{};
  bool gate_open_[2]{true, true};
  float gate_gain_[2]{1.0F, 1.0F};
  float compressor_envelope_[2]{};
};

}  // namespace shipglows::audio
