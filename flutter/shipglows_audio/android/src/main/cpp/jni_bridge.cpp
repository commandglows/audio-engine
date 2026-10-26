#include <jni.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "android_oboe_capture.h"
#include "android_oboe_synth_output.h"
#include "shipglows/audio/analysis_stream.hpp"
#include "shipglows/audio/visual_analysis_wire.hpp"
#include "shipglows/audio/visual_transform_contract.hpp"

namespace {

std::mutex capture_mutex;
std::unique_ptr<shipglows_audio::AndroidOboeCapture> capture;

// Created on first use and kept for the process: every synth session reuses
// the same output, like the shared output bus on Windows.
std::mutex synth_mutex;
std::unique_ptr<shipglows_audio::AndroidOboeSynthOutput> synth_output;

// Output mix analysis of the synth output. The lease bookkeeping (generation,
// ids, profile) lives in Kotlin; this owns the stream and its callback fence.
// Lock order: synth_mutex, then analysis_mutex.
std::mutex analysis_mutex;
std::unique_ptr<shipglows::audio::AnalysisStream> analysis;
bool analysis_stopped = true;

constexpr std::uint32_t kAnalysisMaxBlockFrames = 16384;
constexpr std::size_t kAnalysisMaxNativeBytes = 4 * 1024 * 1024;

shipglows_audio::AndroidOboeSynthOutput& SynthOutput() {
  if (synth_output == nullptr) {
    synth_output = std::make_unique<shipglows_audio::AndroidOboeSynthOutput>();
  }
  return *synth_output;
}

std::string FromJavaString(JNIEnv* environment, jstring value) {
  if (value == nullptr) {
    return {};
  }
  const char* utf8 = environment->GetStringUTFChars(value, nullptr);
  if (utf8 == nullptr) {
    return {};
  }
  std::string result(utf8);
  environment->ReleaseStringUTFChars(value, utf8);
  return result;
}

jstring ToJavaString(JNIEnv* environment, const std::string& value) {
  return environment->NewStringUTF(value.c_str());
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_commandglows_shipglows_1audio_ShipglowsAudioPlugin_nativeCommand(
    JNIEnv* environment, jobject /*plugin*/, jint command,
    jstring session_directory, jint input_device_id) {
  std::lock_guard lock(capture_mutex);
  if (command == 1) {
    capture = std::make_unique<shipglows_audio::AndroidOboeCapture>();
    const auto directory = FromJavaString(environment, session_directory);
    if (directory.empty() ||
        !capture->Start(std::filesystem::path(directory), input_device_id)) {
      return ToJavaString(environment, capture->StatusLine());
    }
  } else if (command == 2 && capture != nullptr) {
    capture->Stop();
  } else if (command == 3 && capture != nullptr) {
    return ToJavaString(environment, capture->Pause());
  } else if (command == 4 && capture != nullptr) {
    return ToJavaString(environment, capture->Resume());
  } else if (command == 5 && capture != nullptr) {
    return ToJavaString(environment,
                        capture->SelectInputDevice(input_device_id));
  } else if (command == 6 && capture != nullptr) {
    capture->SetRecoveryDevice(input_device_id, input_device_id >= 0);
  }
  return ToJavaString(environment,
                      capture == nullptr
                          ? "idle|0|0|unknown|0|0|0|0|0|"
                          : capture->StatusLine());
}

// Synth commands: 1 start, 2 stop, anything else returns the status line.
// A failed start returns the status line with state "error".
extern "C" JNIEXPORT jstring JNICALL
Java_com_commandglows_shipglows_1audio_ShipglowsAudioPlugin_nativeSynthCommand(
    JNIEnv* environment, jobject /*plugin*/, jint command) {
  std::lock_guard lock(synth_mutex);
  auto& output = SynthOutput();
  if (command == 1) {
    (void)output.Start();
  } else if (command == 2) {
    output.Stop();
  }
  return ToJavaString(environment, output.StatusLine());
}

// Queues one synth event; `type` and `instrument` follow SynthEventType and
// SynthInstrument. Returns false when the output is stopped or the queue full.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_commandglows_shipglows_1audio_ShipglowsAudioPlugin_nativeSynthPost(
    JNIEnv* /*environment*/, jobject /*plugin*/, jint type, jint instrument,
    jboolean use_instrument, jboolean accent, jint midi, jfloat velocity) {
  using shipglows::audio::SynthEvent;
  using shipglows::audio::SynthEventType;
  using shipglows::audio::SynthInstrument;
  if (type < 0 || type > 4 || midi < 0 || midi > 127) return JNI_FALSE;
  SynthEvent event;
  event.type = static_cast<SynthEventType>(type);
  event.instrument =
      instrument == 1 ? SynthInstrument::drums : SynthInstrument::piano;
  event.use_instrument = use_instrument == JNI_TRUE;
  event.accent = accent == JNI_TRUE;
  event.midi = static_cast<std::uint8_t>(midi);
  event.velocity = velocity;
  std::lock_guard lock(synth_mutex);
  return SynthOutput().Post(event) ? JNI_TRUE : JNI_FALSE;
}

// Reopens the running synth stream on the current output route.
extern "C" JNIEXPORT void JNICALL
Java_com_commandglows_shipglows_1audio_ShipglowsAudioPlugin_nativeSynthReroute(
    JNIEnv* /*environment*/, jobject /*plugin*/) {
  std::lock_guard lock(synth_mutex);
  SynthOutput().Reroute();
}

// Starts a fresh analysis stream on the synth output. Returns the transform
// digest, or "!<error code>". The caller validates the v2 ranges.
extern "C" JNIEXPORT jstring JNICALL
Java_com_commandglows_shipglows_1audio_ShipglowsAudioPlugin_nativeAnalysisStart(
    JNIEnv* environment, jobject /*plugin*/, jint feature_mask,
    jdouble sensitivity_db, jdouble gamma, jdouble attack_ms,
    jdouble release_ms, jdouble floor_db, jdouble ceiling_db) {
  shipglows::audio::VisualAnalysisConfig config;
  config.sensitivity_db = sensitivity_db;
  config.gamma = gamma;
  config.attack_ms = attack_ms;
  config.release_ms = release_ms;
  config.floor_db = floor_db;
  config.ceiling_db = ceiling_db;
  std::string digest;
  std::unique_ptr<shipglows::audio::AnalysisStream> stream;
  try {
    digest = shipglows::audio::visual_transform_config_digest(config);
    stream = std::make_unique<shipglows::audio::AnalysisStream>(
        static_cast<unsigned>(feature_mask), config, kAnalysisMaxBlockFrames);
  } catch (const std::exception&) {
    return ToJavaString(environment, "!invalid_analysis_configuration");
  }
  if (stream->bytes() > kAnalysisMaxNativeBytes) {
    return ToJavaString(environment, "!analysis_memory_limit");
  }
  std::lock_guard synth_lock(synth_mutex);
  std::lock_guard lock(analysis_mutex);
  auto& output = SynthOutput();
  output.ClearAnalysisStream();
  if (analysis != nullptr) analysis->stop(false);
  analysis = std::move(stream);
  analysis_stopped = false;
  output.SetAnalysisStream(analysis.get());
  return ToJavaString(environment, digest);
}

// Detaches the analysis tap. `release` also frees the stream, after which
// polls fail until the next start.
extern "C" JNIEXPORT void JNICALL
Java_com_commandglows_shipglows_1audio_ShipglowsAudioPlugin_nativeAnalysisStop(
    JNIEnv* /*environment*/, jobject /*plugin*/, jboolean release) {
  std::lock_guard synth_lock(synth_mutex);
  std::lock_guard lock(analysis_mutex);
  SynthOutput().ClearAnalysisStream();
  if (analysis != nullptr && !analysis_stopped) analysis->stop(false);
  analysis_stopped = true;
  if (release == JNI_TRUE) analysis.reset();
}

// Polls the records after `after_sequence`. Returns {metadata, records}:
// metadata is state|continuityId|hasOrigin|originFrame|firstSequence|
// lastSequence|nextSequenceCursor|deliveryResync|resyncReason|
// skippedDeliveryRecords|pcmGap|droppedPcmFrames|nativeObservationNs|
// recordBytes|recordCount and records the concatenated wire records.
// A failure is a one-element array holding "!<error code>".
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_commandglows_shipglows_1audio_ShipglowsAudioPlugin_nativeAnalysisPoll(
    JNIEnv* environment, jobject /*plugin*/, jint feature_mask,
    jlong after_sequence) {
  auto* object_class = environment->FindClass("java/lang/Object");
  const auto fail = [&](const char* code) {
    auto* array = environment->NewObjectArray(1, object_class, nullptr);
    environment->SetObjectArrayElement(array, 0, ToJavaString(environment, code));
    return array;
  };
  std::lock_guard synth_lock(synth_mutex);
  const bool output_failed = SynthOutput().Failed();
  std::lock_guard lock(analysis_mutex);
  if (analysis == nullptr || after_sequence < 0) {
    return fail("!stale_analysis_lease");
  }
  shipglows::audio::AnalysisBatchSnapshot batch;
  try {
    batch = analysis->poll(static_cast<std::uint64_t>(after_sequence));
  } catch (const std::exception&) {
    return fail("!invalid_analysis_cursor");
  }
  if (!analysis_stopped && output_failed) batch.state = "failed";
  static const auto observation_origin = std::chrono::steady_clock::now();
  const auto observation_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - observation_origin)
          .count());
  constexpr std::uint64_t kMaxSafeInteger = 9007199254740991ULL;
  if (batch.continuity_id > kMaxSafeInteger ||
      batch.origin_frame > kMaxSafeInteger ||
      batch.first_sequence > kMaxSafeInteger ||
      batch.last_sequence > kMaxSafeInteger ||
      batch.next_sequence_cursor > kMaxSafeInteger ||
      batch.skipped_delivery_records > kMaxSafeInteger ||
      batch.dropped_pcm_frames > kMaxSafeInteger ||
      observation_ns > kMaxSafeInteger) {
    return fail("!analysis_integer_limit");
  }
  const auto mask = static_cast<unsigned>(feature_mask);
  std::size_t record_bytes = 0;
  std::vector<std::uint8_t> payload;
  try {
    record_bytes = shipglows::audio::visual_analysis_record_bytes(mask);
    payload.reserve(batch.records.size() * record_bytes);
    for (const auto& record : batch.records) {
      auto encoded = shipglows::audio::encode_visual_analysis_record(record, mask);
      payload.insert(payload.end(), encoded.begin(), encoded.end());
    }
  } catch (const std::exception&) {
    return fail("!invalid_analysis_configuration");
  }
  if (payload.size() + 4096 > 131072) return fail("!analysis_response_limit");
  std::ostringstream metadata;
  metadata << batch.state << '|' << batch.continuity_id << '|'
           << (batch.has_origin ? 1 : 0) << '|' << batch.origin_frame << '|'
           << batch.first_sequence << '|' << batch.last_sequence << '|'
           << batch.next_sequence_cursor << '|'
           << (batch.delivery_resync ? 1 : 0) << '|' << batch.resync_reason
           << '|' << batch.skipped_delivery_records << '|'
           << (batch.pcm_gap ? 1 : 0) << '|' << batch.dropped_pcm_frames << '|'
           << observation_ns << '|' << record_bytes << '|'
           << batch.records.size();
  const auto size = static_cast<jsize>(payload.size());
  auto* records = environment->NewByteArray(size);
  environment->SetByteArrayRegion(
      records, 0, size, reinterpret_cast<const jbyte*>(payload.data()));
  auto* array = environment->NewObjectArray(2, object_class, nullptr);
  environment->SetObjectArrayElement(array, 0,
                                     ToJavaString(environment, metadata.str()));
  environment->SetObjectArrayElement(array, 1, records);
  return array;
}
