#include <jni.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "android_oboe_capture.h"

namespace {

std::mutex capture_mutex;
std::unique_ptr<shipglows_audio::AndroidOboeCapture> capture;

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
    jstring session_directory) {
  std::lock_guard lock(capture_mutex);
  if (command == 1) {
    capture = std::make_unique<shipglows_audio::AndroidOboeCapture>();
    const auto directory = FromJavaString(environment, session_directory);
    if (directory.empty() || !capture->Start(std::filesystem::path(directory))) {
      return ToJavaString(environment, capture->StatusLine());
    }
  } else if (command == 2 && capture != nullptr) {
    capture->Stop();
  }
  return ToJavaString(environment,
                      capture == nullptr
                          ? "idle|0|0|unknown|0|0|0|0|0|"
                          : capture->StatusLine());
}
