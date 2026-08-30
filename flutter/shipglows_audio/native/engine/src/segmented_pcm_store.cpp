#include "shipglows/audio/segmented_pcm_store.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace shipglows::audio {
namespace {

std::string segment_file_name(std::uint32_t index) {
  std::ostringstream name;
  name << "segment-" << std::setw(6) << std::setfill('0') << index << ".pcm";
  return name.str();
}

}  // namespace

SegmentedPcmStore::SegmentedPcmStore(std::filesystem::path session_directory,
                                     AudioFormat format,
                                     std::uint64_t frames_per_segment)
    : directory_(std::move(session_directory)),
      format_(format),
      frames_per_segment_(frames_per_segment) {
  if (!format_.valid() || frames_per_segment_ == 0) {
    throw std::invalid_argument("invalid segmented PCM store configuration");
  }
  std::filesystem::create_directories(directory_);
  write_manifest(false);
}

SegmentedPcmStore::~SegmentedPcmStore() {
  if (current_stream_.is_open()) {
    current_stream_.flush();
    current_stream_.close();
  }
}

void SegmentedPcmStore::append(std::span<const std::byte> bytes) {
  if (finalized_) {
    throw std::logic_error("cannot append to a finalized session");
  }
  const auto frame_bytes = format_.bytes_per_frame();
  if (bytes.size() % frame_bytes != 0) {
    throw std::invalid_argument("PCM block is not frame-aligned");
  }

  auto remaining = bytes;
  while (!remaining.empty()) {
    if (!current_stream_.is_open()) {
      open_next_segment();
    }
    const auto available_frames = frames_per_segment_ - current_frames_;
    const auto available_bytes = available_frames * frame_bytes;
    const auto write_bytes = std::min<std::uint64_t>(remaining.size(), available_bytes);
    current_stream_.write(reinterpret_cast<const char*>(remaining.data()),
                          static_cast<std::streamsize>(write_bytes));
    if (!current_stream_) {
      throw std::runtime_error("failed to write PCM segment");
    }
    current_frames_ += write_bytes / frame_bytes;
    remaining = remaining.subspan(static_cast<std::size_t>(write_bytes));
    if (current_frames_ == frames_per_segment_) {
      finalize_current_segment();
    }
  }
}

void SegmentedPcmStore::finalize() {
  if (finalized_) {
    return;
  }
  if (current_stream_.is_open()) {
    finalize_current_segment();
  }
  finalized_ = true;
  write_manifest(true);
}

const std::vector<SegmentRecord>& SegmentedPcmStore::segments() const noexcept {
  return segments_;
}

RecoveryReport SegmentedPcmStore::recover(
    const std::filesystem::path& session_directory, AudioFormat format) {
  if (!format.valid()) {
    throw std::invalid_argument("invalid recovery audio format");
  }
  RecoveryReport report;
  if (!std::filesystem::exists(session_directory)) {
    return report;
  }

  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(session_directory)) {
    const auto name = entry.path().filename().string();
    if (entry.is_regular_file() && name.starts_with("segment-") &&
        entry.path().extension() == ".pcm") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  const auto frame_bytes = format.bytes_per_frame();
  for (std::size_t index = 0; index < files.size(); ++index) {
    const auto size = std::filesystem::file_size(files[index]);
    const auto aligned_size = size - (size % frame_bytes);
    if (aligned_size != size) {
      std::filesystem::resize_file(files[index], aligned_size);
      report.discarded_bytes += size - aligned_size;
    }
    const auto frames = aligned_size / frame_bytes;
    if (frames == 0) {
      continue;
    }
    report.recovered_frames += frames;
    report.segments.push_back({
        .index = static_cast<std::uint32_t>(index),
        .frames = frames,
        .file_name = files[index].filename().string(),
    });
  }
  return report;
}

void SegmentedPcmStore::open_next_segment() {
  const auto name = segment_file_name(static_cast<std::uint32_t>(segments_.size()));
  current_stream_.open(directory_ / name, std::ios::binary | std::ios::trunc);
  if (!current_stream_) {
    throw std::runtime_error("failed to open PCM segment");
  }
  current_frames_ = 0;
}

void SegmentedPcmStore::finalize_current_segment() {
  current_stream_.flush();
  current_stream_.close();
  if (current_frames_ == 0) {
    return;
  }
  const auto index = static_cast<std::uint32_t>(segments_.size());
  segments_.push_back({
      .index = index,
      .frames = current_frames_,
      .file_name = segment_file_name(index),
  });
  current_frames_ = 0;
  write_manifest(false);
}

void SegmentedPcmStore::write_manifest(bool complete) const {
  const auto temporary = directory_ / "manifest.tmp";
  const auto destination = directory_ / "manifest.sga";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("failed to write session manifest");
  }
  output << "schema=shipglows-audio-session/1\n";
  output << "complete=" << (complete ? "true" : "false") << '\n';
  output << "sample_rate=" << format_.sample_rate << '\n';
  output << "channels=" << format_.channel_count << '\n';
  output << "bytes_per_sample=" << format_.bytes_per_sample() << '\n';
  for (const auto& segment : segments_) {
    output << "segment=" << segment.index << ',' << segment.frames << ','
           << segment.file_name << '\n';
  }
  output.flush();
  output.close();
  std::error_code error;
  std::filesystem::remove(destination, error);
  std::filesystem::rename(temporary, destination);
}

}  // namespace shipglows::audio
