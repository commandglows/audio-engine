#include <jni.h>

#include <filesystem>
#include <memory>
#include <cstdint>
#include <mutex>
#include <string>

#include "android_oboe_capture.h"
#include "android_oboe_synth_output.h"

namespace {

std::mutex capture_mutex;
std::unique_ptr<shipglows_audio::AndroidOboeCapture> capture;

// Created on first use and kept for the process: every synth session reuses
// the same output, like the shared output bus on Windows.
std::mutex synth_mutex;
std::unique_ptr<shipglows_audio::AndroidOboeSynthOutput> synth_output;

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
