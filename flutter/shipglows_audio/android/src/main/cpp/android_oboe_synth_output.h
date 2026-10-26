#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "shipglows/audio/dsp_chain.hpp"
#include "shipglows/audio/synth_engine.hpp"

namespace shipglows_audio {

// Live synthesizer output: one low-latency Oboe float stereo stream at 48 kHz
// that renders the shared SynthEngine through the always-on master limiter.
// Oboe resamples when the device runs at another rate.
//
// Threading: Start, Stop, Post and StatusLine belong to the platform thread;
// onAudioReady to the Oboe callback thread. A disconnected stream (headphones
// unplugged, route change) is reopened on a recovery thread while enabled.
class AndroidOboeSynthOutput final : public oboe::AudioStreamDataCallback,
                                     public oboe::AudioStreamErrorCallback {
 public:
  AndroidOboeSynthOutput();
  ~AndroidOboeSynthOutput() override;

  AndroidOboeSynthOutput(const AndroidOboeSynthOutput&) = delete;
  AndroidOboeSynthOutput& operator=(const AndroidOboeSynthOutput&) = delete;

  // Opens and starts the stream if needed. Returns false on failure; the
  // error code is then in StatusLine().
  [[nodiscard]] bool Start();
  // Fades every voice, then stops and closes the stream.
  void Stop();
  // Stamps and queues one event. Refused while stopped (all-notes-off aside).
  [[nodiscard]] bool Post(shipglows::audio::SynthEvent event) noexcept;

  // state|errorCode|instrument|activeVoices|peakActiveVoices|posted|rejected|
  // applied|ignored|stolen|sampleRate|bufferFrames|burstFrames|lowLatency|
  // callbacks|underruns|maxCallbackMicroseconds|queueP50|queueP99|queueMax|
  // outputP50|outputP99|outputMax|outputLatencyMicroseconds
  [[nodiscard]] std::string StatusLine() const;

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream,
                                        void* audio_data,
                                        int32_t num_frames) override;
  void onErrorAfterClose(oboe::AudioStream* stream,
                         oboe::Result error) override;

 private:
  [[nodiscard]] bool OpenLocked();
  void CloseLocked();
  void JoinRecovery();

  mutable std::mutex mutex_;
  std::shared_ptr<oboe::AudioStream> stream_;
  std::unique_ptr<shipglows::audio::SynthEngine> synth_;
  shipglows::audio::DspChain master_{48'000};
  std::string error_code_;
  std::thread recovery_thread_;
  std::atomic<bool> enabled_{false};
  std::atomic<std::uint64_t> callbacks_{0};
  std::atomic<std::uint64_t> max_callback_ns_{0};
  // Latency of the stream measured off the callback, in nanoseconds; the
  // callback adds it to the block start to estimate the presentation time.
  std::atomic<std::uint64_t> output_latency_ns_{0};
};

}  // namespace shipglows_audio
