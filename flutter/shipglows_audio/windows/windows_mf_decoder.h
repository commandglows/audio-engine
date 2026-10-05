#ifndef SHIPGLOWS_AUDIO_WINDOWS_MF_DECODER_H_
#define SHIPGLOWS_AUDIO_WINDOWS_MF_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace shipglows_audio {

enum class MfDecodeError : std::uint8_t {
  none,
  invalid_state,
  file_not_found,
  unsupported_extension,
  unsupported_stream,
  unsupported_profile,
  unsupported_format,
  media_foundation_unavailable,
  source_open_failed,
  decode_failed,
  seek_failed,
  invalid_argument,
};

struct MfDecodeResult final {
  MfDecodeError error = MfDecodeError::none;
  std::string detail;
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == MfDecodeError::none;
  }
};

// All methods, including destruction, must run on the same caller-owned worker
// thread. That thread must initialize COM as MTA before Open(). The decoder does
// no Flutter, device, or callback work. ReadFrames appends interleaved stereo
// float32 at 48 kHz to the caller's vector; a successful zero-frame read is EOF.
class WindowsMfDecoder final {
 public:
  WindowsMfDecoder();
  ~WindowsMfDecoder();
  WindowsMfDecoder(const WindowsMfDecoder&) = delete;
  WindowsMfDecoder& operator=(const WindowsMfDecoder&) = delete;
  WindowsMfDecoder(WindowsMfDecoder&&) noexcept;
  WindowsMfDecoder& operator=(WindowsMfDecoder&&) noexcept;

  [[nodiscard]] MfDecodeResult Open(const std::filesystem::path& path);
  [[nodiscard]] MfDecodeResult ReadFrames(std::size_t max_frames,
                                           std::vector<float>& stereo);
  [[nodiscard]] MfDecodeResult Seek(double seconds);
  [[nodiscard]] double duration_seconds() const noexcept;
  [[nodiscard]] bool is_open() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace shipglows_audio

#endif  // SHIPGLOWS_AUDIO_WINDOWS_MF_DECODER_H_
