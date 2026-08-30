#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace shipglows::audio {

class Sha256 final {
 public:
  void update(std::span<const std::byte> bytes) noexcept;
  [[nodiscard]] std::array<std::byte, 32> finalize() noexcept;
  [[nodiscard]] static std::string hex(
      const std::array<std::byte, 32>& digest);

 private:
  void transform(const std::byte* block) noexcept;

  std::array<std::uint32_t, 8> state_{
      0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::byte, 64> buffer_{};
  std::uint64_t total_bytes_ = 0;
  std::size_t buffered_ = 0;
  bool finalized_ = false;
};

[[nodiscard]] std::string sha256_file(const std::filesystem::path& path);

}  // namespace shipglows::audio
