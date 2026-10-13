// Decoder trial runner for WindowsMfDecoder. Usage: shipglows_decoder_trials <fixture-dir>
//
// For each fixture in fixture_specs.h it checks, as applicable:
//   - Open() outcome matches the expectation (accept, exact typed reject, or observe)
//   - rejected inputs leave is_open() false
//   - total duration within tolerance; output is interleaved stereo at 48 kHz
//   - dominant frequency of each output channel within +/-2 % of the tone
//   - no clipping (peak < 0.999) and not silent
//   - seek to the midpoint succeeds and the remainder has the right length and tone
// Prints a matrix and returns non-zero on any mismatch. Fixtures marked
// "not generated" by the generator are reported and do not fail the run.

#include "fixture_specs.h"
#include "windows_mf_decoder.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace shipglows_audio;
using namespace shipglows_audio::trials;

namespace {

constexpr double kOutputRate = 48000.0;
constexpr double kFrequencyTolerance = 0.02;  // +/-2 %
constexpr double kPrimingSkipSeconds = 0.25;  // skip encoder delay/pre-roll
constexpr double kSeekSkipSeconds = 0.05;     // skip post-seek pre-roll
constexpr double kPeakCeiling = 0.999;
constexpr double kPeakFloor = 0.1;
constexpr std::size_t kMaxFftFrames = 65536;

const char* Name(MfDecodeError error) {
  switch (error) {
    case MfDecodeError::none: return "none";
    case MfDecodeError::invalid_state: return "invalid_state";
    case MfDecodeError::file_not_found: return "file_not_found";
    case MfDecodeError::unsupported_extension: return "unsupported_extension";
    case MfDecodeError::unsupported_stream: return "unsupported_stream";
    case MfDecodeError::unsupported_profile: return "unsupported_profile";
    case MfDecodeError::unsupported_format: return "unsupported_format";
    case MfDecodeError::media_foundation_unavailable: return "media_foundation_unavailable";
    case MfDecodeError::source_open_failed: return "source_open_failed";
    case MfDecodeError::decode_failed: return "decode_failed";
    case MfDecodeError::seek_failed: return "seek_failed";
    case MfDecodeError::invalid_argument: return "invalid_argument";
  }
  return "unknown";
}

std::string Fixed(double value, int digits) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
  return buffer;
}

// Iterative radix-2 FFT; size must be a power of two.
void Fft(std::vector<std::complex<double>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
    const std::complex<double> step(std::cos(angle), std::sin(angle));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (std::size_t k = 0; k < len / 2; ++k) {
        const auto u = a[i + k];
        const auto v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= step;
      }
    }
  }
}

// Dominant spectral peak in 30 Hz..20 kHz of a Hann-windowed block, refined
// by parabolic interpolation on log magnitude. Returns NaN if too short.
double DominantHz(const std::vector<double>& x, double rate) {
  std::size_t n = 1;
  while (n * 2 <= std::min(x.size(), kMaxFftFrames)) n *= 2;
  if (n < 4096) return std::nan("");
  std::vector<std::complex<double>> a(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (n - 1));
    a[i] = x[i] * w;
  }
  Fft(a);
  std::vector<double> mag(n / 2);
  for (std::size_t k = 0; k < n / 2; ++k) mag[k] = std::abs(a[k]);
  const auto k_min = static_cast<std::size_t>(std::ceil(30.0 * n / rate));
  const auto k_max = std::min<std::size_t>(
      static_cast<std::size_t>(std::floor(20000.0 * n / rate)), n / 2 - 2);
  std::size_t best = k_min;
  for (std::size_t k = k_min; k <= k_max; ++k)
    if (mag[k] > mag[best]) best = k;
  const double a0 = std::log(mag[best - 1] + 1e-12);
  const double b0 = std::log(mag[best] + 1e-12);
  const double c0 = std::log(mag[best + 1] + 1e-12);
  const double denom = a0 - 2.0 * b0 + c0;
  const double shift = denom == 0.0 ? 0.0 : 0.5 * (a0 - c0) / denom;
  return (static_cast<double>(best) + shift) * rate / static_cast<double>(n);
}

struct Manifest {
  std::string status;
  std::string detail;
};

std::map<std::string, Manifest> LoadManifest(const fs::path& dir) {
  std::map<std::string, Manifest> result;
  std::ifstream in(dir / "generated.tsv");
  std::string line;
  std::getline(in, line);  // header
  while (std::getline(in, line)) {
    const auto first = line.find('\t');
    const auto second = line.find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos) continue;
    result[line.substr(0, first)] = {line.substr(first + 1, second - first - 1),
                                     line.substr(second + 1)};
  }
  return result;
}

struct Row {
  std::string file;
  std::string expected;
  std::string actual;
  std::string duration;
  std::string frequency;
  std::string peak;
  std::string seek;
  std::string verdict;
  std::string notes;
  bool failed = false;
};

void Fail(Row& row, const std::string& why) {
  row.failed = true;
  if (!row.notes.empty()) row.notes += "; ";
  row.notes += why;
}

std::string ExpectText(const Spec& spec) {
  switch (spec.expect) {
    case Expect::Accept: return "accept";
    case Expect::Observe: return "observe";
    case Expect::Reject: return std::string("reject:") + Name(spec.error);
  }
  return "?";
}

// Deinterleaves the stereo output into one channel.
std::vector<double> Channel(const std::vector<float>& stereo, int channel) {
  std::vector<double> out(stereo.size() / 2);
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i] = stereo[i * 2 + static_cast<std::size_t>(channel)];
  return out;
}

double PeakOf(const std::vector<float>& stereo) {
  double peak = 0.0;
  for (const float s : stereo) peak = std::max(peak, static_cast<double>(std::fabs(s)));
  return peak;
}

// Reads frames until EOF. Returns the first read error (or none).
MfDecodeResult ReadToEnd(WindowsMfDecoder& decoder, std::vector<float>& out,
                         std::size_t frame_cap) {
  std::vector<float> chunk;
  for (;;) {
    const auto result = decoder.ReadFrames(4096, chunk);
    if (!result) return result;
    if (chunk.empty()) return {};  // zero-frame successful read is EOF
    out.insert(out.end(), chunk.begin(), chunk.end());
    if (out.size() / 2 > frame_cap)
      return {MfDecodeError::decode_failed, "frame_cap_exceeded"};
  }
}

bool FrequencyMatches(const std::vector<float>& stereo, double skip_seconds,
                      double expected_left, double expected_right,
                      std::string& text) {
  const auto skip = static_cast<std::size_t>(skip_seconds * kOutputRate) * 2;
  if (stereo.size() <= skip) {
    text = "too_short";
    return false;
  }
  const std::vector<float> body(stereo.begin() + static_cast<std::ptrdiff_t>(skip),
                                stereo.end());
  const double left = DominantHz(Channel(body, 0), kOutputRate);
  const double right = DominantHz(Channel(body, 1), kOutputRate);
  const bool left_ok =
      std::fabs(left - expected_left) <= kFrequencyTolerance * expected_left;
  const bool right_ok =
      std::fabs(right - expected_right) <= kFrequencyTolerance * expected_right;
  text = Fixed(left, 2) + "/" + Fixed(right, 2) + "Hz";
  return left_ok && right_ok;
}

Row Evaluate(const Spec& spec, const fs::path& dir,
             const std::map<std::string, Manifest>& manifest) {
  Row row;
  row.file = spec.file;
  row.expected = ExpectText(spec);
  row.duration = "-";
  row.frequency = "-";
  row.peak = "-";
  row.seek = "-";

  const auto found = manifest.find(spec.file);
  if (found == manifest.end() || found->second.status != "generated") {
    row.actual = "not generated";
    row.verdict = "NOT GENERATED";
    row.notes = found == manifest.end() ? "missing from manifest" : found->second.detail;
    return row;
  }

  WindowsMfDecoder decoder;
  const MfDecodeResult open = decoder.Open(dir / spec.file);
  row.actual = open ? "accept" : std::string("reject:") + Name(open.error);
  if (!open) row.notes = open.detail;

  if (spec.expect == Expect::Reject) {
    if (open) Fail(row, "accepted out-of-profile input");
    else if (open.error != spec.error) Fail(row, "wrong error category");
    if (!open && decoder.is_open()) Fail(row, "is_open() true after failed Open");
    row.verdict = row.failed ? "FAIL" : "PASS";
    return row;
  }

  if (!open) {
    if (spec.expect == Expect::Observe) {
      row.verdict = "OBSERVED";
      return row;
    }
    Fail(row, "open failed");
    row.verdict = "FAIL";
    return row;
  }

  std::vector<float> stereo;
  const auto frame_cap = static_cast<std::size_t>(spec.seconds * kOutputRate * 2) +
                         static_cast<std::size_t>(kOutputRate * 2);
  const MfDecodeResult read = ReadToEnd(decoder, stereo, frame_cap);
  if (!read) Fail(row, std::string("read: ") + Name(read.error) + " " + read.detail);
  const double frames = static_cast<double>(stereo.size() / 2);
  const double seconds = frames / kOutputRate;
  row.duration = Fixed(seconds, 3) + "s (exp " + Fixed(spec.seconds, 2) + ")";
  if (stereo.size() % 2 != 0) Fail(row, "odd sample count (not stereo)");

  if (spec.expect == Expect::Observe) {
    row.actual = "accept";
    row.verdict = "OBSERVED";
    row.notes = "frames=" + Fixed(frames, 0) + (row.notes.empty() ? "" : "; " + row.notes);
    row.duration = Fixed(seconds, 3) + "s";
    return row;
  }

  const double tolerance = spec.compressed ? 0.10 : 0.02;
  if (std::fabs(seconds - spec.seconds) > tolerance)
    Fail(row, "duration outside " + Fixed(tolerance, 2) + "s tolerance");

  const double peak = PeakOf(stereo);
  row.peak = Fixed(peak, 4);
  if (peak >= kPeakCeiling) Fail(row, "clipped");
  if (peak < kPeakFloor) Fail(row, "silent or too quiet");

  std::string frequency_text;
  if (!FrequencyMatches(stereo, kPrimingSkipSeconds, spec.freq_left_hz,
                        spec.freq_right_hz, frequency_text))
    Fail(row, "dominant frequency outside 2%");
  row.frequency = frequency_text + " (exp " + Fixed(spec.freq_left_hz, 0) + "/" +
                  Fixed(spec.freq_right_hz, 0) + ")";

  // Seek to the midpoint, then read to EOF and verify the remainder.
  const double total = decoder.duration_seconds() > 0.0 ? decoder.duration_seconds()
                                                         : seconds;
  const double seek_to = total / 2.0;
  const MfDecodeResult seek = decoder.Seek(seek_to);
  if (!seek) {
    Fail(row, std::string("seek: ") + Name(seek.error));
    row.seek = "seek failed";
  } else {
    std::vector<float> tail;
    const MfDecodeResult tail_read = ReadToEnd(decoder, tail, frame_cap);
    const double tail_seconds = static_cast<double>(tail.size() / 2) / kOutputRate;
    const double expected_tail = spec.seconds - seek_to;
    const double tail_tolerance = spec.compressed ? 0.15 : 0.05;
    row.seek = "@" + Fixed(seek_to, 2) + "s tail " + Fixed(tail_seconds, 3) + "s";
    if (!tail_read) Fail(row, std::string("seek read: ") + Name(tail_read.error));
    if (tail.empty()) Fail(row, "no frames after seek");
    else if (std::fabs(tail_seconds - expected_tail) > tail_tolerance)
      Fail(row, "post-seek length off");
    std::string tail_frequency;
    if (!tail.empty() && !FrequencyMatches(tail, kSeekSkipSeconds, spec.freq_left_hz,
                                           spec.freq_right_hz, tail_frequency))
      Fail(row, "post-seek frequency outside 2%");
    row.seek += " " + tail_frequency;
  }

  row.verdict = row.failed ? "FAIL" : "PASS";
  return row;
}

void PrintTable(const std::vector<Row>& rows) {
  std::vector<std::size_t> widths{4, 30, 24, 14, 24, 24, 10, 30, 13};
  const std::vector<std::string> headers{"#", "fixture", "expected", "actual",
                                         "duration", "L/R freq", "peak", "seek", "verdict"};
  auto line = [&](const std::vector<std::string>& cells) {
    std::ostringstream text;
    for (std::size_t i = 0; i < cells.size(); ++i) {
      std::string cell = cells[i];
      if (cell.size() > widths[i]) cell = cell.substr(0, widths[i] - 1) + "~";
      text << cell << std::string(widths[i] - cell.size() + 2, ' ');
    }
    std::cout << text.str() << '\n';
  };
  line(headers);
  std::size_t index = 0;
  for (const Row& row : rows) {
    ++index;
    line({std::to_string(index), row.file, row.expected, row.actual, row.duration,
          row.frequency, row.peak, row.seek, row.verdict});
  }
  std::cout << "\nNotes:\n";
  index = 0;
  for (const Row& row : rows) {
    ++index;
    if (!row.notes.empty())
      std::cout << "  " << index << " " << row.file << ": " << row.notes << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: shipglows_decoder_trials <fixture-dir>\n";
    return 2;
  }
  const fs::path dir = argv[1];
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
    std::cerr << "CoInitializeEx(MTA) failed\n";
    return 2;
  }
  const auto manifest = LoadManifest(dir);
  if (manifest.empty()) {
    std::cerr << "no generated.tsv in " << dir.string() << " (run shipglows_fixture_gen)\n";
    CoUninitialize();
    return 2;
  }

  std::vector<Row> rows;
  for (const Spec& spec : kSpecs) rows.push_back(Evaluate(spec, dir, manifest));
  PrintTable(rows);

  int passed = 0, failed = 0, not_generated = 0, observed = 0;
  for (const Row& row : rows) {
    if (row.verdict == "PASS") ++passed;
    else if (row.verdict == "FAIL") ++failed;
    else if (row.verdict == "NOT GENERATED") ++not_generated;
    else ++observed;
  }
  std::cout << "\nSummary: " << passed << " pass, " << failed << " fail, " << observed
            << " observed, " << not_generated << " not generated (of " << rows.size()
            << ")\n";
  CoUninitialize();
  return failed == 0 ? 0 : 1;
}
