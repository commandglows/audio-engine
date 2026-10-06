#include <flutter/method_call.h>
#include <flutter/method_result_functions.h>
#include <flutter/standard_method_codec.h>
#include <gtest/gtest.h>
#include <windows.h>

#include <memory>
#include <string>
#include <variant>

#include "shipglows_audio_plugin.h"
#include "windows_wasapi_capture.h"
#include "windows_wasapi_playback.h"

namespace shipglows_audio {
namespace test {

namespace {

using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResultFunctions;

}  // namespace

TEST(ShipglowsAudioPlugin, GetEngineInfo) {
  ShipglowsAudioPlugin plugin;
  EncodableMap result_map;
  plugin.HandleMethodCall(
      MethodCall("getEngineInfo", std::make_unique<EncodableValue>()),
      std::make_unique<MethodResultFunctions<>>(
          [&result_map](const EncodableValue* result) {
            result_map = std::get<EncodableMap>(*result);
          },
          nullptr, nullptr));

  EXPECT_EQ(std::get<std::string>(
                result_map.at(EncodableValue("version"))),
            "0.1.0");
  EXPECT_TRUE(std::get<bool>(
      result_map.at(EncodableValue("nativeCoreLoaded"))));
}

TEST(WindowsWasapiPlayback, ExposesStableFailureCodes) {
  EXPECT_STREQ(WasapiPlaybackFailureCode(WasapiPlaybackFailure::none), "");
  EXPECT_STREQ(WasapiPlaybackFailureCode(WasapiPlaybackFailure::device_unavailable),
               "wasapi_device_unavailable");
  EXPECT_STREQ(WasapiPlaybackFailureCode(WasapiPlaybackFailure::invalid_endpoint),
               "wasapi_invalid_endpoint");
  EXPECT_STREQ(WasapiPlaybackFailureCode(WasapiPlaybackFailure::stream_failed),
               "wasapi_stream_failed");
}

TEST(WindowsWasapiCapture, KeepsLostSelectedEndpoint) {
  EXPECT_EQ(ClassifyWasapiRouteChange(true, false),
            WasapiRouteChange::selected_device_lost);
}

TEST(WindowsWasapiCapture, FollowsIntentionalDefaultChange) {
  EXPECT_EQ(ClassifyWasapiRouteChange(true, true),
            WasapiRouteChange::default_device_changed);
}

TEST(WindowsWasapiCapture, IgnoresUnchangedDefault) {
  EXPECT_EQ(ClassifyWasapiRouteChange(false, false), WasapiRouteChange::none);
}

TEST(WindowsWasapiCapture, RecreatesTerminalCaptureForNewSession) {
  EXPECT_TRUE(NeedsFreshWasapiCapture(
      shipglows::audio::SessionState::stopped));
  EXPECT_TRUE(NeedsFreshWasapiCapture(shipglows::audio::SessionState::failed));
  EXPECT_FALSE(NeedsFreshWasapiCapture(shipglows::audio::SessionState::idle));
  EXPECT_FALSE(
      NeedsFreshWasapiCapture(shipglows::audio::SessionState::recording));
}

TEST(WindowsWasapiCapture, ClassifiesPowerBroadcasts) {
  EXPECT_EQ(ClassifyWasapiPowerBroadcast(PBT_APMSUSPEND),
            WasapiPowerEvent::suspend);
  EXPECT_EQ(ClassifyWasapiPowerBroadcast(PBT_APMRESUMEAUTOMATIC),
            WasapiPowerEvent::resume);
  EXPECT_EQ(ClassifyWasapiPowerBroadcast(PBT_APMRESUMESUSPEND),
            WasapiPowerEvent::resume);
  EXPECT_EQ(ClassifyWasapiPowerBroadcast(PBT_APMPOWERSTATUSCHANGE),
            WasapiPowerEvent::none);
}

TEST(WindowsWasapiCapture, DeduplicatesResumeBroadcasts) {
  EXPECT_TRUE(ShouldQueueWasapiResume(true, false, false));
  EXPECT_TRUE(ShouldQueueWasapiResume(false, true, false));
  EXPECT_FALSE(ShouldQueueWasapiResume(true, false, true));
  EXPECT_FALSE(ShouldQueueWasapiResume(false, false, false));
}

}  // namespace test
}  // namespace shipglows_audio
