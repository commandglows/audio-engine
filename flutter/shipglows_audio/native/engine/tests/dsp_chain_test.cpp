#include "shipglows/audio/dsp_chain.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
constexpr std::uint32_t kSampleRate = 48'000;
constexpr float kPi = 3.14159265358979323846F;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::vector<float> sine(float amplitude, float frequency, std::size_t frames) {
  std::vector<float> samples(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    const float value = amplitude * std::sin(
        2.0F * kPi * frequency * static_cast<float>(i) / kSampleRate);
    samples[i * 2] = value;
    samples[i * 2 + 1] = value;
  }
  return samples;
}

double rms(const std::vector<float>& samples, std::size_t first_frame) {
  double sum = 0;
  const auto frames = samples.size() / 2;
  for (std::size_t frame = first_frame; frame < frames; ++frame) {
    sum += static_cast<double>(samples[frame * 2]) * samples[frame * 2];
  }
  return std::sqrt(sum / static_cast<double>(frames - first_frame));
}
}  // namespace

int main() {
  {
    auto samples = sine(0.25F, 440.0F, 4'800);
    const auto original = samples;
    shipglows::audio::DspChain chain(kSampleRate);
    chain.process(samples.data(), samples.size() / 2);
    expect(samples == original, "disabled DSP must be a bit-stable passthrough");
  }

  {
    auto samples = sine(0.2F, 1'000.0F, 48'000);
    shipglows::audio::DspChain chain(kSampleRate);
    shipglows::audio::DspParameters parameters;
    parameters.eq_enabled = true;
    parameters.mid_gain_db = 6.0F;
    expect(chain.set_parameters(parameters), "valid EQ parameters must queue");
    const auto before = rms(samples, 4'800);
    chain.process(samples.data(), samples.size() / 2);
    expect(rms(samples, 4'800) > before * 1.7,
           "mid-band EQ must increase a 1 kHz test tone");
  }

  {
    auto samples = sine(0.8F, 440.0F, 48'000);
    shipglows::audio::DspChain chain(kSampleRate);
    shipglows::audio::DspParameters parameters;
    parameters.compressor_enabled = true;
    parameters.compressor_threshold_dbfs = -18.0F;
    parameters.compressor_ratio = 4.0F;
    expect(chain.set_parameters(parameters), "valid compressor parameters must queue");
    const auto before = rms(samples, 12'000);
    chain.process(samples.data(), samples.size() / 2);
    expect(rms(samples, 12'000) < before * 0.6,
           "compressor must reduce a signal above its threshold");
  }

  {
    shipglows::audio::DspChain chain(kSampleRate);
    shipglows::audio::DspParameters parameters;
    parameters.low_gain_db = 100.0F;
    expect(!chain.set_parameters(parameters), "out-of-range EQ must be rejected");
  }
  return 0;
}
