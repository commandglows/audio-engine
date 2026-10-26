#include "android_oboe_synth_output.h"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

namespace shipglows_audio {
namespace {

constexpr char kLogTag[] = "shipglows_synth";
constexpr int kRecoveryAttempts = 5;

std::uint64_t SteadyNanoseconds() noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

const char* InstrumentName(shipglows::audio::SynthInstrument instrument) {
  return instrument == shipglows::audio::SynthInstrument::drums ? "drums"
                                                                : "piano";
}

}  // namespace

AndroidOboeSynthOutput::AndroidOboeSynthOutput()
    : synth_(std::make_unique<shipglows::audio::SynthEngine>()) {}

AndroidOboeSynthOutput::~AndroidOboeSynthOutput() { Stop(); }

bool AndroidOboeSynthOutput::Start() {
  std::lock_guard lock(mutex_);
  enabled_.store(true, std::memory_order_release);
  if (stream_ != nullptr &&
      stream_->getState() == oboe::StreamState::Started) {
    return true;
  }
  CloseLocked();
  if (OpenLocked()) return true;
  enabled_.store(false, std::memory_order_release);
  return false;
}

void AndroidOboeSynthOutput::Stop() {
  if (enabled_.exchange(false, std::memory_order_acq_rel)) {
    shipglows::audio::SynthEvent off;
    off.type = shipglows::audio::SynthEventType::all_notes_off;
    off.post_ns = SteadyNanoseconds();
    (void)synth_->post(off);
    // Let the 5 ms all-notes-off fade reach the device before releasing it.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (synth_->counters().active_voices != 0 &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  JoinRecovery();
  std::lock_guard lock(mutex_);
  CloseLocked();
}

bool AndroidOboeSynthOutput::Post(shipglows::audio::SynthEvent event) noexcept {
  if (!enabled_.load(std::memory_order_acquire) &&
      event.type != shipglows::audio::SynthEventType::all_notes_off) {
    return false;
  }
  event.post_ns = SteadyNanoseconds();
  return synth_->post(event);
}

bool AndroidOboeSynthOutput::OpenLocked() {
  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Output)
      ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
      ->setSharingMode(oboe::SharingMode::Exclusive)
      ->setUsage(oboe::Usage::Game)
      ->setContentType(oboe::ContentType::Music)
      ->setFormat(oboe::AudioFormat::Float)
      ->setFormatConversionAllowed(true)
      ->setChannelCount(oboe::ChannelCount::Stereo)
      ->setChannelConversionAllowed(true)
      ->setSampleRate(static_cast<int32_t>(
          shipglows::audio::SynthEngine::kSampleRate))
      ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
      ->setDataCallback(this)
      ->setErrorCallback(this);
  std::shared_ptr<oboe::AudioStream> stream;
  auto result = builder.openStream(stream);
  if (result != oboe::Result::OK) {
    error_code_ = std::string("output_open_failed:") + oboe::convertToText(result);
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", error_code_.c_str());
    return false;
  }
  // Two bursts keep the latency low while absorbing scheduling jitter.
  (void)stream->setBufferSizeInFrames(stream->getFramesPerBurst() * 2);
  // The callback is not running yet, so the consumer-side resets are safe.
  // Events queued while the stream was down would otherwise play as a burst.
  synth_->reset();
  master_.reset();
  stream_ = stream;
  result = stream_->requestStart();
  if (result != oboe::Result::OK) {
    error_code_ = std::string("output_start_failed:") + oboe::convertToText(result);
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", error_code_.c_str());
    CloseLocked();
    return false;
  }
  error_code_.clear();
  const auto latency = stream_->calculateLatencyMillis();
  output_latency_ns_.store(
      latency ? static_cast<std::uint64_t>(latency.value() * 1'000'000.0) : 0,
      std::memory_order_relaxed);
  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "output started: %d Hz, %d ch, burst %d, buffer %d, %s, %s",
                      stream_->getSampleRate(), stream_->getChannelCount(),
                      stream_->getFramesPerBurst(),
                      stream_->getBufferSizeInFrames(),
                      oboe::convertToText(stream_->getPerformanceMode()),
                      oboe::convertToText(stream_->getSharingMode()));
  return true;
}

void AndroidOboeSynthOutput::CloseLocked() {
  if (stream_ == nullptr) return;
  (void)stream_->stop();
  (void)stream_->close();
  stream_.reset();
}

void AndroidOboeSynthOutput::JoinRecovery() {
  std::thread recovery;
  {
    std::lock_guard lock(mutex_);
    recovery = std::move(recovery_thread_);
  }
  if (recovery.joinable()) recovery.join();
}

oboe::DataCallbackResult AndroidOboeSynthOutput::onAudioReady(
    oboe::AudioStream* stream, void* audio_data, int32_t num_frames) {
  const auto start_ns = SteadyNanoseconds();
  auto* output = static_cast<float*>(audio_data);
  const auto frames = static_cast<std::uint32_t>(std::max(num_frames, 0));
  const auto channels = static_cast<std::uint32_t>(stream->getChannelCount());
  if (channels == 2) {
    std::memset(output, 0, sizeof(float) * frames * 2);
    const auto latency_ns = output_latency_ns_.load(std::memory_order_relaxed);
    synth_->render_add(output, frames, start_ns,
                       latency_ns == 0 ? 0 : start_ns + latency_ns);
    master_.process(output, frames);
  } else {
    std::memset(output, 0, sizeof(float) * frames * channels);
  }
  callbacks_.fetch_add(1, std::memory_order_relaxed);
  const auto elapsed = SteadyNanoseconds() - start_ns;
  auto previous = max_callback_ns_.load(std::memory_order_relaxed);
  while (elapsed > previous &&
         !max_callback_ns_.compare_exchange_weak(previous, elapsed,
                                                 std::memory_order_relaxed)) {
  }
  return oboe::DataCallbackResult::Continue;
}

void AndroidOboeSynthOutput::onErrorAfterClose(oboe::AudioStream* stream,
                                               oboe::Result error) {
  __android_log_print(ANDROID_LOG_WARN, kLogTag, "output closed: %s",
                      oboe::convertToText(error));
  std::thread previous;
  {
    std::lock_guard lock(mutex_);
    if (stream_.get() != stream) return;
    stream_.reset();
    if (!enabled_.load(std::memory_order_acquire)) return;
    error_code_ = std::string("output_disconnected:") + oboe::convertToText(error);
    previous = std::move(recovery_thread_);
    recovery_thread_ = std::thread([this] {
      for (int attempt = 0; attempt < kRecoveryAttempts; ++attempt) {
        {
          std::lock_guard lock(mutex_);
          if (!enabled_.load(std::memory_order_acquire) || stream_ != nullptr) {
            return;
          }
          if (OpenLocked()) return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    });
  }
  if (previous.joinable()) previous.join();
}

std::string AndroidOboeSynthOutput::StatusLine() const {
  std::lock_guard lock(mutex_);
  const bool enabled = enabled_.load(std::memory_order_acquire);
  const bool running = stream_ != nullptr &&
                       stream_->getState() == oboe::StreamState::Started;
  const char* state = !error_code_.empty() && !running
                          ? "error"
                          : (enabled ? "running" : "stopped");
  const auto counters = synth_->counters();
  const auto queue = synth_->queue_latency();
  const auto output = synth_->output_latency();
  std::int32_t underruns = 0;
  if (stream_ != nullptr) {
    const auto xruns = stream_->getXRunCount();
    if (xruns) underruns = xruns.value();
  }
  std::ostringstream line;
  line << state << '|' << error_code_ << '|' << InstrumentName(synth_->instrument())
       << '|' << counters.active_voices << '|' << counters.peak_active_voices
       << '|' << counters.posted << '|' << counters.rejected << '|'
       << counters.applied << '|' << counters.ignored << '|'
       << counters.stolen_voices << '|'
       << (stream_ ? stream_->getSampleRate() : 0) << '|'
       << (stream_ ? stream_->getBufferSizeInFrames() : 0) << '|'
       << (stream_ ? stream_->getFramesPerBurst() : 0) << '|'
       << (stream_ && stream_->getPerformanceMode() ==
                          oboe::PerformanceMode::LowLatency
               ? 1
               : 0)
       << '|' << callbacks_.load(std::memory_order_relaxed) << '|' << underruns
       << '|' << max_callback_ns_.load(std::memory_order_relaxed) / 1000 << '|'
       << queue.p50_us << '|' << queue.p99_us << '|' << queue.max_us << '|'
       << output.p50_us << '|' << output.p99_us << '|' << output.max_us << '|'
       << output_latency_ns_.load(std::memory_order_relaxed) / 1000;
  return line.str();
}

}  // namespace shipglows_audio
