#pragma once
#include <cmath>
#include <cstdint>
namespace shipglows_audio {
// Called on the pre-mix output timeline only, once per recorded stereo frame.
// -60 dBFS peak threshold. Paused/discarded frames never reach this counter.
class OutputActivityCounter final {
 public:
  void Observe(float left, float right) {
    ++active_frames_;
    if ((std::isfinite(left) && std::abs(left) > 0.001f) ||
        (std::isfinite(right) && std::abs(right) > 0.001f)) silent_frames_ = 0;
    else ++silent_frames_;
  }
  uint64_t active_milliseconds() const { return active_frames_ / 48; }
  uint64_t silent_milliseconds() const { return silent_frames_ / 48; }
 private:
  uint64_t active_frames_ = 0;
  uint64_t silent_frames_ = 0;
};
}
