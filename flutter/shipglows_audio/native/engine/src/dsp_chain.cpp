#include "shipglows/audio/dsp_chain.hpp"

#include <algorithm>
#include <cmath>

namespace shipglows::audio {
namespace {
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTruePeakCeiling = 0.84139514F;  // -1.5 dBTP internal guard.
float db_to_gain(float db) noexcept { return std::pow(10.0F, db / 20.0F); }
bool in_range(float v, float lo, float hi) noexcept {
  return std::isfinite(v) && v >= lo && v <= hi;
}

float sinc(float x) noexcept {
  if (std::abs(x) < 1.0e-7F) return 1.0F;
  const float px = kPi * x;
  return std::sin(px) / px;
}

template <typename Filter>
void peaking(Filter& b, float rate, float frequency, float q, float gain_db) noexcept {
  const float a = db_to_gain(gain_db);
  const float w = 2.0F * kPi * frequency / rate;
  const float alpha = std::sin(w) / (2.0F * q);
  const float c = std::cos(w);
  const float a0 = 1.0F + alpha / a;
  b.b0 = (1.0F + alpha * a) / a0;
  b.b1 = (-2.0F * c) / a0;
  b.b2 = (1.0F - alpha * a) / a0;
  b.a1 = (-2.0F * c) / a0;
  b.a2 = (1.0F - alpha / a) / a0;
}
template <typename Filter>
void shelf(Filter& b, float rate, float frequency, float gain_db, bool high) noexcept {
  const float a = db_to_gain(gain_db);
  const float w = 2.0F * kPi * frequency / rate;
  const float c = std::cos(w), s = std::sin(w);
  const float alpha = s / std::sqrt(2.0F);
  const float root = 2.0F * std::sqrt(a) * alpha;
  const float a0 = (a + 1.0F) + (a - 1.0F) * c + root;
  if (high) {
    b.b0 = a * ((a + 1.0F) + (a - 1.0F) * c + root) / a0;
    b.b1 = -2.0F * a * ((a - 1.0F) + (a + 1.0F) * c) / a0;
    b.b2 = a * ((a + 1.0F) + (a - 1.0F) * c - root) / a0;
    b.a1 = 2.0F * ((a - 1.0F) - (a + 1.0F) * c) / a0;
    b.a2 = ((a + 1.0F) - (a - 1.0F) * c - root) / a0;
  } else {
    b.b0 = a * ((a + 1.0F) - (a - 1.0F) * c + root) / a0;
    b.b1 = 2.0F * a * ((a - 1.0F) - (a + 1.0F) * c) / a0;
    b.b2 = a * ((a + 1.0F) - (a - 1.0F) * c - root) / a0;
    b.a1 = -2.0F * ((a - 1.0F) + (a + 1.0F) * c) / a0;
    b.a2 = ((a + 1.0F) + (a - 1.0F) * c - root) / a0;
  }
}
}  // namespace

float DspChain::Biquad::run(float x) noexcept {
  const float y = b0 * x + z1;
  z1 = b1 * x - a1 * y + z2;
  z2 = b2 * x - a2 * y;
  return y;
}

DspChain::DspChain(std::uint32_t sample_rate) noexcept
    : sample_rate_(std::clamp(sample_rate, 8'000U, 192'000U)) {
  // Build the short windowed-sinc interpolation table off the audio callback.
  // Runtime peak detection performs only fixed-size multiply-accumulates.
  for (std::uint32_t phase = 1; phase < kLimiterPhases; ++phase) {
    const float fraction = static_cast<float>(phase) / kLimiterPhases;
    float sum = 0.0F;
    for (std::uint32_t tap = 0; tap < kLimiterTaps; ++tap) {
      const float distance = fraction - (static_cast<int>(tap) - 15);
      const float window = 0.42F + 0.5F * std::cos(kPi * distance / 16.0F) +
                           0.08F * std::cos(2.0F * kPi * distance / 16.0F);
      const float coefficient = std::abs(distance) < 16.0F
          ? sinc(distance) * window : 0.0F;
      limiter_coefficients_[phase - 1][tap] = coefficient;
      sum += coefficient;
    }
    if (std::abs(sum) > 1.0e-7F) {
      for (std::uint32_t tap = 0; tap < kLimiterTaps; ++tap) {
        limiter_coefficients_[phase - 1][tap] /= sum;
      }
    }
  }
  for (int channel = 0; channel < 2; ++channel) {
    shelf(low_[channel], static_cast<float>(sample_rate_), 120, 0.0F, false);
    peaking(mid_[channel], static_cast<float>(sample_rate_), 1000, 0.7F, 0.0F);
    shelf(high_[channel], static_cast<float>(sample_rate_), 8000, 0.0F, true);
  }
}

bool DspChain::set_parameters(const DspParameters& p) noexcept {
  if (!in_range(p.low_gain_db, -12, 12) || !in_range(p.mid_gain_db, -12, 12) ||
      !in_range(p.high_gain_db, -12, 12) || !in_range(p.gate_threshold_dbfs, -80, 0) ||
      !in_range(p.gate_release_ms, 10, 500) ||
      !in_range(p.compressor_threshold_dbfs, -60, 0) ||
      !in_range(p.compressor_ratio, 1, 20) || !in_range(p.compressor_attack_ms, 1, 100) ||
      !in_range(p.compressor_release_ms, 10, 1000)) return false;
  // SPSC producer path: if the consumer has not reclaimed a slot, reject this
  // update rather than waiting or overwriting data still in use.
  const std::uint32_t head = update_head_.load(std::memory_order_relaxed);
  const std::uint32_t tail = update_tail_.load(std::memory_order_acquire);
  if (head - tail >= kUpdateQueueCapacity) return false;
  Update& update = updates_[head % kUpdateQueueCapacity];
  update.parameters = p;
  for (int channel = 0; channel < 2; ++channel) {
    shelf(update.low[channel], static_cast<float>(sample_rate_), 120, p.low_gain_db, false);
    peaking(update.mid[channel], static_cast<float>(sample_rate_), 1000, 0.7F, p.mid_gain_db);
    shelf(update.high[channel], static_cast<float>(sample_rate_), 8000, p.high_gain_db, true);
  }
  update_head_.store(head + 1, std::memory_order_release);
  return true;
}

void DspChain::apply_pending_updates() noexcept {
  std::uint32_t tail = update_tail_.load(std::memory_order_relaxed);
  const std::uint32_t head = update_head_.load(std::memory_order_acquire);
  while (tail != head) {
    const Update& update = updates_[tail % kUpdateQueueCapacity];
    parameters_ = update.parameters;
    for (int channel = 0; channel < 2; ++channel) {
      low_[channel].b0 = update.low[channel].b0;
      low_[channel].b1 = update.low[channel].b1;
      low_[channel].b2 = update.low[channel].b2;
      low_[channel].a1 = update.low[channel].a1;
      low_[channel].a2 = update.low[channel].a2;
      mid_[channel].b0 = update.mid[channel].b0;
      mid_[channel].b1 = update.mid[channel].b1;
      mid_[channel].b2 = update.mid[channel].b2;
      mid_[channel].a1 = update.mid[channel].a1;
      mid_[channel].a2 = update.mid[channel].a2;
      high_[channel].b0 = update.high[channel].b0;
      high_[channel].b1 = update.high[channel].b1;
      high_[channel].b2 = update.high[channel].b2;
      high_[channel].a1 = update.high[channel].a1;
      high_[channel].a2 = update.high[channel].a2;
    }
    ++tail;
  }
  update_tail_.store(tail, std::memory_order_release);
}

void DspChain::reset() noexcept {
  for (int ch = 0; ch < 2; ++ch) {
    low_[ch].z1 = low_[ch].z2 = mid_[ch].z1 = mid_[ch].z2 = high_[ch].z1 = high_[ch].z2 = 0;
    gate_envelope_[ch] = compressor_envelope_[ch] = 0;
    gate_open_[ch] = true;
    gate_gain_[ch] = 1.0F;
  }
  for (auto& frame : limiter_ring_) frame[0] = frame[1] = 0.0F;
  for (auto& peak : limiter_peak_ring_) peak = 0.0F;
  limiter_frame_count_ = 0;
  limiter_gain_ = 1.0F;
}

void DspChain::process(float* samples, std::size_t frames) noexcept {
  if (samples == nullptr) return;
  apply_pending_updates();
  const float gate_threshold = db_to_gain(parameters_.gate_threshold_dbfs);
  const float gate_release = std::exp(-1.0F / (0.001F * parameters_.gate_release_ms * sample_rate_));
  const float comp_attack = std::exp(-1.0F / (0.001F * parameters_.compressor_attack_ms * sample_rate_));
  const float comp_release = std::exp(-1.0F / (0.001F * parameters_.compressor_release_ms * sample_rate_));
  const float limiter_release = std::exp(
      -1.0F / (0.050F * static_cast<float>(sample_rate_)));
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (int ch = 0; ch < 2; ++ch) {
      float x = samples[frame * 2 + ch];
      if (!std::isfinite(x)) x = 0;
      if (parameters_.eq_enabled) x = high_[ch].run(mid_[ch].run(low_[ch].run(x)));
      if (parameters_.gate_enabled) {
        const float coefficient = std::abs(x) > gate_envelope_[ch] ? 0.0F : gate_release;
        gate_envelope_[ch] = coefficient * gate_envelope_[ch] + (1.0F - coefficient) * std::abs(x);
        if (gate_open_[ch] && gate_envelope_[ch] < gate_threshold) gate_open_[ch] = false;
        if (!gate_open_[ch] && gate_envelope_[ch] >= gate_threshold * db_to_gain(6.0F)) gate_open_[ch] = true;
        if (gate_open_[ch]) {
          gate_gain_[ch] = 1.0F;
        } else {
          gate_gain_[ch] = 0.001F + gate_release * (gate_gain_[ch] - 0.001F);
        }
        x *= gate_gain_[ch];
      } else {
        gate_open_[ch] = true;
        gate_gain_[ch] = 1.0F;
      }
      if (parameters_.compressor_enabled) {
        const float magnitude = std::abs(x);
        const float coefficient = magnitude > compressor_envelope_[ch] ? comp_attack : comp_release;
        compressor_envelope_[ch] = coefficient * compressor_envelope_[ch] + (1.0F - coefficient) * magnitude;
        const float level_db = 20.0F * std::log10(std::max(compressor_envelope_[ch], 1.0e-9F));
        const float threshold_db = parameters_.compressor_threshold_dbfs;
        const float over_db = level_db - threshold_db;
        float reduction_db = 0.0F;
        if (over_db > 3.0F) {
          reduction_db = over_db * (1.0F - 1.0F / parameters_.compressor_ratio);
        } else if (over_db > -3.0F) {
          const float knee_position = over_db + 3.0F;
          reduction_db = knee_position * knee_position / 12.0F *
                         (1.0F - 1.0F / parameters_.compressor_ratio);
        }
        x *= db_to_gain(-reduction_db);
      }
      limiter_ring_[limiter_frame_count_ % kLimiterRingFrames][ch] = x;
    }

    auto sample_at = [this](std::int64_t index, int channel) noexcept {
      if (index < 0) return 0.0F;
      return limiter_ring_[static_cast<std::uint64_t>(index) %
                           kLimiterRingFrames][channel];
    };
    // Measure one 64-phase, 32-tap reconstructed interval as its FIR future
    // becomes available. The rolling peak ring then gives each output sample
    // a fixed 16-interval lookahead, holding gain through attack and clip edges.
    if (limiter_frame_count_ + 1 >= kLimiterFirLookaheadFrames) {
      const auto interval = static_cast<std::int64_t>(limiter_frame_count_) -
                            kLimiterFirLookaheadFrames;
      float peak = 0.0F;
      for (int ch = 0; ch < 2; ++ch) {
        peak = std::max(peak, std::abs(sample_at(interval, ch)));
        peak = std::max(peak, std::abs(sample_at(interval + 1, ch)));
      }
      for (std::uint32_t phase = 1; phase < kLimiterPhases; ++phase) {
        for (int ch = 0; ch < 2; ++ch) {
          float interpolated = 0.0F;
          for (std::uint32_t tap = 0; tap < kLimiterTaps; ++tap) {
            interpolated += sample_at(interval + static_cast<int>(tap) - 15, ch) *
                            limiter_coefficients_[phase - 1][tap];
          }
          peak = std::max(peak, std::abs(interpolated));
        }
      }
      const auto peak_slot = (interval + kLimiterRingFrames) % kLimiterRingFrames;
      limiter_peak_ring_[static_cast<std::uint32_t>(peak_slot)] = peak;
    }

    if (limiter_frame_count_ >= kLimiterDelayFrames) {
      const auto output_frame = static_cast<std::int64_t>(
          limiter_frame_count_ - kLimiterDelayFrames);
      float peak = 0.0F;
      for (std::uint32_t ahead = 0; ahead < kLimiterPeakWindowFrames; ++ahead) {
        const auto interval = output_frame - 1 + ahead;
        const auto peak_slot = (interval + kLimiterRingFrames) % kLimiterRingFrames;
        peak = std::max(peak,
                        limiter_peak_ring_[static_cast<std::uint32_t>(peak_slot)]);
      }
      const float target_gain = peak > kTruePeakCeiling
          ? kTruePeakCeiling / peak : 1.0F;
      if (target_gain < limiter_gain_) {
        limiter_gain_ = target_gain;
      } else {
        limiter_gain_ = limiter_release * limiter_gain_ +
                        (1.0F - limiter_release) * target_gain;
      }
      for (int ch = 0; ch < 2; ++ch) {
        samples[frame * 2 + ch] = sample_at(output_frame, ch) * limiter_gain_;
      }
    } else {
      samples[frame * 2] = 0.0F;
      samples[frame * 2 + 1] = 0.0F;
    }
    ++limiter_frame_count_;
  }
}

}  // namespace shipglows::audio
