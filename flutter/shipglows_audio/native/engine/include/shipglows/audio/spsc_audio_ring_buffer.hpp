#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace shipglows::audio {

template <typename Sample>
class SpscAudioRingBuffer final {
 public:
  explicit SpscAudioRingBuffer(std::size_t capacity)
      : storage_(capacity + 1) {}

  SpscAudioRingBuffer(const SpscAudioRingBuffer&) = delete;
  SpscAudioRingBuffer& operator=(const SpscAudioRingBuffer&) = delete;

  [[nodiscard]] std::size_t capacity() const noexcept {
    return storage_.empty() ? 0 : storage_.size() - 1;
  }

  [[nodiscard]] std::size_t available_to_read() const noexcept {
    const auto read = read_index_.load(std::memory_order_acquire);
    const auto write = write_index_.load(std::memory_order_acquire);
    return write >= read ? write - read : storage_.size() - read + write;
  }

  [[nodiscard]] std::size_t push(std::span<const Sample> input) noexcept {
    if (storage_.size() <= 1 || input.empty()) {
      return 0;
    }
    const auto write = write_index_.load(std::memory_order_relaxed);
    const auto read = read_index_.load(std::memory_order_acquire);
    const auto free = read > write ? read - write - 1
                                   : storage_.size() - write + read - 1;
    const auto count = std::min(input.size(), free);
    for (std::size_t index = 0; index < count; ++index) {
      storage_[(write + index) % storage_.size()] = input[index];
    }
    write_index_.store((write + count) % storage_.size(),
                       std::memory_order_release);
    return count;
  }

  [[nodiscard]] std::size_t pop(std::span<Sample> output) noexcept {
    if (storage_.size() <= 1 || output.empty()) {
      return 0;
    }
    const auto read = read_index_.load(std::memory_order_relaxed);
    const auto write = write_index_.load(std::memory_order_acquire);
    const auto available = write >= read ? write - read
                                         : storage_.size() - read + write;
    const auto count = std::min(output.size(), available);
    for (std::size_t index = 0; index < count; ++index) {
      output[index] = storage_[(read + index) % storage_.size()];
    }
    read_index_.store((read + count) % storage_.size(),
                      std::memory_order_release);
    return count;
  }

 private:
  std::vector<Sample> storage_;
  alignas(64) std::atomic<std::size_t> read_index_{0};
  alignas(64) std::atomic<std::size_t> write_index_{0};
};

}  // namespace shipglows::audio
