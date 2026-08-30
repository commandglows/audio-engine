#include "shipglows/audio/segmented_wav_store.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "shipglows/audio/sha256.hpp"

namespace shipglows::audio {
namespace {

constexpr std::uint32_t wav_header_bytes = 44;

std::string segment_name(std::uint32_t index) {
  std::ostringstream name;
  name << "segment-" << std::setw(6) << std::setfill('0') << index << ".wav";
  return name.str();
}

void write_u16(std::ostream& output, std::uint16_t value) {
  const std::array<char, 2> bytes{
      static_cast<char>(value & 0xffU),
      static_cast<char>((value >> 8U) & 0xffU)};
  output.write(bytes.data(), bytes.size());
}

void write_u32(std::ostream& output, std::uint32_t value) {
  const std::array<char, 4> bytes{
      static_cast<char>(value & 0xffU),
      static_cast<char>((value >> 8U) & 0xffU),
      static_cast<char>((value >> 16U) & 0xffU),
      static_cast<char>((value >> 24U) & 0xffU)};
  output.write(bytes.data(), bytes.size());
}

void write_header(std::ostream& output, AudioFormat format,
                  std::uint32_t data_bytes) {
  output.write("RIFF", 4);
  write_u32(output, 36U + data_bytes);
  output.write("WAVEfmt ", 8);
  write_u32(output, 16);
  write_u16(output, format.sample_format == SampleFormat::float32 ? 3 : 1);
  write_u16(output, format.channel_count);
  write_u32(output, format.sample_rate);
  write_u32(output, format.sample_rate * format.bytes_per_frame());
  write_u16(output, static_cast<std::uint16_t>(format.bytes_per_frame()));
  write_u16(output, static_cast<std::uint16_t>(format.bytes_per_sample() * 8));
  output.write("data", 4);
  write_u32(output, data_bytes);
}

}  // namespace

SegmentedWavStore::SegmentedWavStore(
    std::filesystem::path session_directory, AudioFormat format,
    std::uint64_t frames_per_segment)
    : directory_(std::move(session_directory)),
      format_(format),
      frames_per_segment_(frames_per_segment) {
  if (!format_.valid() || frames_per_segment_ == 0) {
    throw std::invalid_argument("invalid segmented WAV store configuration");
  }
  const auto maximum_data = frames_per_segment_ * format_.bytes_per_frame();
  if (maximum_data > std::numeric_limits<std::uint32_t>::max() - 36U) {
    throw std::invalid_argument("WAV segment exceeds RIFF size limit");
  }
  std::filesystem::create_directories(directory_);
  journal_.open(directory_ / "journal.sga", std::ios::binary | std::ios::trunc);
  if (!journal_) throw std::runtime_error("failed to open session journal");
  append_journal("schema=shipglows-audio-session/2");
  append_journal("event=session_open");
  append_journal("format=" + std::to_string(format_.sample_rate) + "," +
                 std::to_string(format_.channel_count) + "," +
                 std::to_string(format_.bytes_per_sample()));
}

SegmentedWavStore::~SegmentedWavStore() {
  if (current_stream_.is_open()) current_stream_.close();
  if (journal_.is_open()) journal_.close();
}

void SegmentedWavStore::append(std::span<const std::byte> bytes) {
  if (finalized_) throw std::logic_error("cannot append to finalized session");
  const auto frame_bytes = format_.bytes_per_frame();
  if (bytes.size() % frame_bytes != 0) {
    throw std::invalid_argument("WAV block is not frame-aligned");
  }
  auto remaining = bytes;
  while (!remaining.empty()) {
    if (!current_stream_.is_open()) open_next_segment();
    const auto available = (frames_per_segment_ - current_frames_) * frame_bytes;
    const auto write_bytes = std::min<std::uint64_t>(remaining.size(), available);
    current_stream_.write(reinterpret_cast<const char*>(remaining.data()),
                          static_cast<std::streamsize>(write_bytes));
    if (!current_stream_) throw std::runtime_error("failed to write WAV segment");
    current_frames_ += write_bytes / frame_bytes;
    remaining = remaining.subspan(static_cast<std::size_t>(write_bytes));
    if (current_frames_ == frames_per_segment_) finalize_current_segment();
  }
}

void SegmentedWavStore::finalize() {
  if (finalized_) return;
  if (current_stream_.is_open()) finalize_current_segment();
  append_journal("event=session_complete,total_frames=" +
                 std::to_string(total_frames_));
  finalized_ = true;
  journal_.close();
}

const std::vector<WavSegmentRecord>& SegmentedWavStore::segments() const noexcept {
  return segments_;
}

std::uint64_t SegmentedWavStore::total_frames() const noexcept {
  return total_frames_;
}

void SegmentedWavStore::open_next_segment() {
  const auto name = segment_name(static_cast<std::uint32_t>(segments_.size()));
  current_stream_.open(directory_ / name, std::ios::binary | std::ios::trunc);
  if (!current_stream_) throw std::runtime_error("failed to open WAV segment");
  write_header(current_stream_, format_, 0);
  current_frames_ = 0;
}

void SegmentedWavStore::finalize_current_segment() {
  const auto data_bytes = current_frames_ * format_.bytes_per_frame();
  current_stream_.seekp(0);
  write_header(current_stream_, format_, static_cast<std::uint32_t>(data_bytes));
  current_stream_.flush();
  current_stream_.close();
  if (current_frames_ == 0) return;

  const auto index = static_cast<std::uint32_t>(segments_.size());
  const auto name = segment_name(index);
  const auto start_frame = total_frames_;
  total_frames_ += current_frames_;
  const auto file_bytes = wav_header_bytes + data_bytes;
  const auto digest = sha256_file(directory_ / name);
  if (digest.empty()) throw std::runtime_error("failed to hash WAV segment");
  segments_.push_back({
      .index = index,
      .start_frame = start_frame,
      .frames = current_frames_,
      .file_bytes = file_bytes,
      .sha256 = digest,
      .file_name = name,
  });
  append_journal("segment=" + std::to_string(index) + "," +
                 std::to_string(start_frame) + "," +
                 std::to_string(current_frames_) + "," +
                 std::to_string(file_bytes) + "," + digest + "," + name);
  current_frames_ = 0;
}

void SegmentedWavStore::append_journal(const std::string& record) {
  journal_ << record << '\n';
  journal_.flush();
  if (!journal_) throw std::runtime_error("failed to commit session journal");
}

}  // namespace shipglows::audio
