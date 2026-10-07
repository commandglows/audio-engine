#include "shipglows/audio/dsp_chain.hpp"

#include <algorithm>
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

double sinc(double x) {
  if (std::abs(x) < 1.0e-12) return 1.0;
  const double px = kPi * x;
  return std::sin(px) / px;
}

// Independent 64x Lanczos-windowed sinc reconstruction for test assertions.
double true_peak_64x(const std::vector<float>& samples, std::size_t first_frame,
                     std::size_t last_frame, double* peak_position = nullptr) {
  double peak = 0;
  for (std::size_t frame = first_frame; frame < last_frame; ++frame) {
    for (int phase = 0; phase <= 64; ++phase) {
      const double position = static_cast<double>(frame) + phase / 64.0;
      const auto center = static_cast<std::int64_t>(std::floor(position));
      double weighted = 0, weight_sum = 0;
      for (std::int64_t source = center - 15; source <= center + 16; ++source) {
        if (source < 0 || source >= static_cast<std::int64_t>(samples.size() / 2)) continue;
        const double distance = position - static_cast<double>(source);
        if (std::abs(distance) >= 16.0) continue;
        const double window = 0.42 + 0.5 * std::cos(kPi * distance / 16.0) +
                              0.08 * std::cos(2.0 * kPi * distance / 16.0);
        const double weight = sinc(distance) * window;
        weighted += samples[static_cast<std::size_t>(source) * 2] * weight;
        weight_sum += weight;
      }
      if (std::abs(weight_sum) > 1.0e-12) {
        const double candidate = std::abs(weighted / weight_sum);
        if (candidate > peak) {
          peak = candidate;
          if (peak_position) *peak_position = position;
        }
      }
    }
  }
  return peak;
}

std::vector<float> process_in_chunks(std::vector<float> samples,
                                     std::size_t block_size) {
  shipglows::audio::DspChain chain(kSampleRate);
  for (std::size_t offset = 0; offset < samples.size() / 2;) {
    const std::size_t count = std::min(block_size, samples.size() / 2 - offset);
    chain.process(samples.data() + offset * 2, count);
    offset += count;
  }
  return samples;
}
}  // namespace

int main() {
  {
    auto samples = sine(0.25F, 440.0F, 4'800);
    const auto original = samples;
    shipglows::audio::DspChain chain(kSampleRate);
    chain.process(samples.data(), samples.size() / 2);
    for (std::size_t frame = 0; frame < 32; ++frame) {
      expect(samples[frame * 2] == 0.0F && samples[frame * 2 + 1] == 0.0F,
             "the fixed limiter lookahead must produce thirty-two startup frames");
    }
    for (std::size_t frame = 32; frame < original.size() / 2; ++frame) {
      expect(samples[frame * 2] == original[(frame - 32) * 2] &&
                 samples[frame * 2 + 1] == original[(frame - 32) * 2 + 1],
             "quiet disabled DSP must remain a bit-stable delayed passthrough");
    }
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

  {
    constexpr std::size_t frames = 6'000;
    constexpr double frequency = 17'123.0;
    constexpr double phase = 0.37;
    constexpr double input_amplitude = 1.5;
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const float value = static_cast<float>(input_amplitude * std::sin(
          2.0 * kPi * frequency * static_cast<double>(frame) / kSampleRate + phase));
      samples[frame * 2] = value;
      samples[frame * 2 + 1] = value;
    }
    const auto input_peak = true_peak_64x(samples, 64, frames - 64);
    expect(input_peak > std::pow(10.0, -1.0 / 20.0),
           "the limiter fixture must exceed the -1 dBTP ceiling");

    shipglows::audio::DspChain chain(kSampleRate);
    // Exercise callback boundaries: limiter history and gain must persist.
    for (std::size_t offset = 0; offset < frames;) {
      const std::size_t count = std::min<std::size_t>(127, frames - offset);
      chain.process(samples.data() + offset * 2, count);
      offset += count;
    }
    const auto output_peak = true_peak_64x(samples, 64, frames - 64);
    if (output_peak > std::pow(10.0, -1.0 / 20.0)) {
      std::cerr << "measured 64x peak: " << output_peak << '\n';
    }
    expect(output_peak <= std::pow(10.0, -1.0 / 20.0),
           "the always-on limiter must hold independent 64x true peak to -1 dBTP");
  }

  {
    constexpr std::size_t content_frames = 2'048;
    constexpr std::size_t flush_frames = 48;
    const double ceiling = std::pow(10.0, -1.0 / 20.0);
    auto verify_fixture = [&](std::vector<float> content, std::size_t block_size,
                              const char* name) {
      const auto input_peak = true_peak_64x(content, 0, content_frames);
      expect(input_peak > ceiling, name);
      content.resize((content_frames + flush_frames) * 2, 0.0F);
      const auto output = process_in_chunks(std::move(content), block_size);
      double peak_position = 0;
      const auto full_peak = true_peak_64x(
          output, 0, content_frames + 32, &peak_position);
      if (full_peak > ceiling) {
        std::cerr << name << " 64x output peak: " << full_peak
                  << " at frame " << peak_position << '\n';
        if (peak_position < 64.0) {
          for (std::size_t i = 28; i < 40; ++i) {
            std::cerr << i << ':' << output[i * 2] << ' ';
          }
          std::cerr << '\n';
        }
      }
      expect(full_peak <= ceiling,
          "independent 64x measurement must stay below the ceiling in every fixture window");
      return output;
    };

    std::vector<float> impulses(content_frames * 2, 0.0F);
    impulses[0] = impulses[1] = 1.5F;
    impulses[(content_frames - 1) * 2] = -1.5F;
    impulses[(content_frames - 1) * 2 + 1] = -1.5F;
    const auto impulse_output = verify_fixture(
        std::move(impulses), 127, "impulse fixture must exceed -1 dBTP");
        expect(true_peak_64x(impulse_output, 0, 64) <= ceiling &&
               true_peak_64x(impulse_output, content_frames + 16,
                             content_frames + 48) <= ceiling,
           "first and final impulse windows must both be limited after silence flush");

    std::vector<float> nyquist(content_frames * 2);
    for (std::size_t frame = 0; frame < content_frames; ++frame) {
      const float value = frame % 2 == 0 ? 1.25F : -1.25F;
      nyquist[frame * 2] = nyquist[frame * 2 + 1] = value;
    }
    verify_fixture(std::move(nyquist), 61,
                   "alternating Nyquist fixture must exceed -1 dBTP");

    std::vector<float> multisine(content_frames * 2);
    std::uint32_t random_state = 0x6d2b79f5U;
    std::vector<float> noise(content_frames * 2);
    for (std::size_t frame = 0; frame < content_frames; ++frame) {
      const double t = static_cast<double>(frame) / kSampleRate;
      const float tone = static_cast<float>(
          0.8 * std::sin(2.0 * kPi * 7'013.0 * t + 0.23) +
          0.55 * std::sin(2.0 * kPi * 14'321.0 * t + 1.11) +
          0.38 * std::sin(2.0 * kPi * 20'117.0 * t + 2.07));
      multisine[frame * 2] = multisine[frame * 2 + 1] = tone;
      random_state = random_state * 1'664'525U + 1'013'904'223U;
      const float random = static_cast<float>(random_state >> 8) /
                           static_cast<float>(0x00ffffffU);
      const float sample = (random * 2.0F - 1.0F) * 1.25F;
      noise[frame * 2] = noise[frame * 2 + 1] = sample;
    }
    verify_fixture(std::move(multisine), 113,
                   "multisine fixture must exceed -1 dBTP");
    verify_fixture(std::move(noise), 97,
                   "fixed-seed noise fixture must exceed -1 dBTP");
  }
  return 0;
}
