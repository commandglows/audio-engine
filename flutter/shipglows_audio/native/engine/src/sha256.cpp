#include "shipglows/audio/sha256.hpp"

#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>

namespace shipglows::audio {
namespace {

constexpr std::array<std::uint32_t, 64> constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::uint32_t rotate_right(std::uint32_t value,
                                     std::uint32_t count) noexcept {
  return (value >> count) | (value << (32U - count));
}

}  // namespace

void Sha256::update(std::span<const std::byte> bytes) noexcept {
  if (finalized_) return;
  total_bytes_ += bytes.size();
  while (!bytes.empty()) {
    const auto count = std::min(bytes.size(), buffer_.size() - buffered_);
    std::copy_n(bytes.begin(), count, buffer_.begin() + buffered_);
    buffered_ += count;
    bytes = bytes.subspan(count);
    if (buffered_ == buffer_.size()) {
      transform(buffer_.data());
      buffered_ = 0;
    }
  }
}

std::array<std::byte, 32> Sha256::finalize() noexcept {
  if (!finalized_) {
    const auto bit_length = total_bytes_ * 8U;
    buffer_[buffered_++] = std::byte{0x80};
    if (buffered_ > 56) {
      std::fill(buffer_.begin() + buffered_, buffer_.end(), std::byte{0});
      transform(buffer_.data());
      buffered_ = 0;
    }
    std::fill(buffer_.begin() + buffered_, buffer_.begin() + 56, std::byte{0});
    for (std::size_t index = 0; index < 8; ++index) {
      buffer_[63 - index] =
          static_cast<std::byte>((bit_length >> (index * 8U)) & 0xffU);
    }
    transform(buffer_.data());
    finalized_ = true;
  }
  std::array<std::byte, 32> digest{};
  for (std::size_t word = 0; word < state_.size(); ++word) {
    for (std::size_t byte = 0; byte < 4; ++byte) {
      digest[word * 4 + byte] = static_cast<std::byte>(
          (state_[word] >> ((3 - byte) * 8U)) & 0xffU);
    }
  }
  return digest;
}

std::string Sha256::hex(const std::array<std::byte, 32>& digest) {
  std::ostringstream value;
  value << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    value << std::setw(2) << std::to_integer<unsigned int>(byte);
  }
  return value.str();
}

void Sha256::transform(const std::byte* block) noexcept {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0; index < 16; ++index) {
    words[index] =
        (std::to_integer<std::uint32_t>(block[index * 4]) << 24U) |
        (std::to_integer<std::uint32_t>(block[index * 4 + 1]) << 16U) |
        (std::to_integer<std::uint32_t>(block[index * 4 + 2]) << 8U) |
        std::to_integer<std::uint32_t>(block[index * 4 + 3]);
  }
  for (std::size_t index = 16; index < words.size(); ++index) {
    const auto s0 = rotate_right(words[index - 15], 7) ^
                    rotate_right(words[index - 15], 18) ^
                    (words[index - 15] >> 3U);
    const auto s1 = rotate_right(words[index - 2], 17) ^
                    rotate_right(words[index - 2], 19) ^
                    (words[index - 2] >> 10U);
    words[index] = words[index - 16] + s0 + words[index - 7] + s1;
  }

  auto working = state_;
  for (std::size_t index = 0; index < words.size(); ++index) {
    const auto choice =
        (working[4] & working[5]) ^ (~working[4] & working[6]);
    const auto majority = (working[0] & working[1]) ^
                          (working[0] & working[2]) ^
                          (working[1] & working[2]);
    const auto sum1 = rotate_right(working[4], 6) ^
                      rotate_right(working[4], 11) ^
                      rotate_right(working[4], 25);
    const auto sum0 = rotate_right(working[0], 2) ^
                      rotate_right(working[0], 13) ^
                      rotate_right(working[0], 22);
    const auto temporary1 = working[7] + sum1 + choice + constants[index] +
                            words[index];
    const auto temporary2 = sum0 + majority;
    working[7] = working[6];
    working[6] = working[5];
    working[5] = working[4];
    working[4] = working[3] + temporary1;
    working[3] = working[2];
    working[2] = working[1];
    working[1] = working[0];
    working[0] = temporary1 + temporary2;
  }
  for (std::size_t index = 0; index < state_.size(); ++index) {
    state_[index] += working[index];
  }
}

std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  Sha256 hash;
  std::array<std::byte, 64 * 1024> bytes{};
  while (input) {
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    hash.update(std::span(bytes).first(static_cast<std::size_t>(input.gcount())));
  }
  return Sha256::hex(hash.finalize());
}

}  // namespace shipglows::audio
