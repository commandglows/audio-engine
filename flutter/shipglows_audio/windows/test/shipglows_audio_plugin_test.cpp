#include <flutter/method_call.h>
#include <flutter/method_result_functions.h>
#include <flutter/standard_method_codec.h>
#include <gtest/gtest.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>

#include "shipglows_audio_plugin.h"
#include "windows_wasapi_capture.h"
#include "windows_wasapi_playback.h"

namespace shipglows_audio {
namespace test {

namespace {

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResultFunctions;

struct CallOutcome {
  bool success = false;
  std::string error_code;
  EncodableValue value;
};

CallOutcome Call(ShipglowsAudioPlugin& plugin, const std::string& method,
                 EncodableMap arguments) {
  CallOutcome outcome;
  plugin.HandleMethodCall(
      MethodCall(method, std::make_unique<EncodableValue>(std::move(arguments))),
      std::make_unique<MethodResultFunctions<>>(
          [&outcome](const EncodableValue* result) {
            outcome.success = true;
            if (result) outcome.value = *result;
          },
          [&outcome](const std::string& code, const std::string&, const EncodableValue*) {
            outcome.error_code = code;
          },
          nullptr));
  return outcome;
}

EncodableMap SynthArgs(int64_t generation) {
  return EncodableMap{{EncodableValue("synthApiVersion"), EncodableValue(1)},
                      {EncodableValue("generation"), EncodableValue(generation)}};
}

EncodableMap TransformConfig() {
  return EncodableMap{{EncodableValue("version"), EncodableValue(1)},
                      {EncodableValue("sensitivity_db"), EncodableValue(0.0)},
                      {EncodableValue("amplitude_gamma"), EncodableValue(0.8)},
                      {EncodableValue("attack_ms"), EncodableValue(20.0)},
                      {EncodableValue("release_ms"), EncodableValue(180.0)},
                      {EncodableValue("spectrum_floor_db"), EncodableValue(-80.0)},
                      {EncodableValue("spectrum_ceiling_db"), EncodableValue(0.0)}};
}

EncodableMap MixAnalysisArgs(int64_t generation) {
  return EncodableMap{{EncodableValue("analysisApiVersion"), EncodableValue(2)},
                      {EncodableValue("generation"), EncodableValue(generation)},
                      {EncodableValue("profile"), EncodableValue("sgav-music-1")},
                      {EncodableValue("featureMask"), EncodableValue(11)},
                      {EncodableValue("transformConfig"), EncodableValue(TransformConfig())},
                      {EncodableValue("source"), EncodableValue("windows_output_mix_v1")}};
}

const EncodableValue& Field(const CallOutcome& outcome, const char* key) {
  return std::get<EncodableMap>(outcome.value).at(EncodableValue(key));
}

int64_t IntField(const EncodableValue& value) {
  if (const auto* narrow = std::get_if<int32_t>(&value)) return *narrow;
  return std::get<int64_t>(value);
}

bool IsDeviceFailure(const std::string& code) {
  return code.rfind("wasapi_", 0) == 0;
}

// 16-bit stereo 48 kHz sine, enough for Media Foundation to decode.
std::filesystem::path WriteToneWav(double seconds) {
  const auto path = std::filesystem::temp_directory_path() / "shipglows_synth_coexistence.wav";
  const auto frames = static_cast<uint32_t>(seconds * 48000);
  const uint32_t data_bytes = frames * 4;
  std::ofstream out(path, std::ios::binary);
  const auto u32 = [&out](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  const auto u16 = [&out](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
  out.write("RIFF", 4); u32(36 + data_bytes); out.write("WAVE", 4);
  out.write("fmt ", 4); u32(16); u16(1); u16(2); u32(48000); u32(192000); u16(4); u16(16);
  out.write("data", 4); u32(data_bytes);
  for (uint32_t i = 0; i < frames; ++i) {
    const auto sample = static_cast<int16_t>(3000 * std::sin(2 * 3.141592653589793 * 220 * i / 48000));
    out.write(reinterpret_cast<const char*>(&sample), 2);
    out.write(reinterpret_cast<const char*>(&sample), 2);
  }
  return path;
}

EncodableMap PlaybackArgs(int64_t generation) {
  return EncodableMap{{EncodableValue("apiVersion"), EncodableValue(1)},
                      {EncodableValue("generation"), EncodableValue(generation)}};
}

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

TEST(ShipglowsAudioPlugin, AdvertisesOutputMixAnalysisSource) {
  ShipglowsAudioPlugin plugin;
  const auto caps = Call(plugin, "getAnalysisCapabilities",
                         {{EncodableValue("analysisApiVersion"), EncodableValue(2)}});
  ASSERT_TRUE(caps.success);
  bool mix = false;
  for (const auto& source : std::get<EncodableList>(Field(caps, "sources"))) {
    const auto& map = std::get<EncodableMap>(source);
    if (std::get<std::string>(map.at(EncodableValue("id"))) == "windows_output_mix_v1")
      mix = std::get<bool>(map.at(EncodableValue("available")));
  }
  EXPECT_TRUE(mix);
}

TEST(ShipglowsAudioPlugin, SynthRequiresItsApiVersion) {
  ShipglowsAudioPlugin plugin;
  auto args = SynthArgs(1);
  args[EncodableValue("synthApiVersion")] = EncodableValue(2);
  args[EncodableValue("instrument")] = EncodableValue("piano");
  EXPECT_EQ(Call(plugin, "startSynth", args).error_code, "unsupported_synth_api");
}

TEST(ShipglowsAudioPlugin, SynthCommandsAreFencedBeforeStart) {
  ShipglowsAudioPlugin plugin;
  auto note = SynthArgs(1);
  note[EncodableValue("midi")] = EncodableValue(60);
  note[EncodableValue("velocity")] = EncodableValue(0.8);
  EXPECT_EQ(Call(plugin, "noteOn", note).error_code, "stale_synth_generation");
  auto start = SynthArgs(1);
  start[EncodableValue("instrument")] = EncodableValue("organ");
  EXPECT_EQ(Call(plugin, "startSynth", start).error_code, "invalid_synth_request");
}

TEST(ShipglowsAudioPlugin, MixAnalysisRequiresRunningSynth) {
  ShipglowsAudioPlugin plugin;
  EXPECT_EQ(Call(plugin, "startAnalysis", MixAnalysisArgs(1)).error_code,
            "stale_analysis_generation");
  auto unknown = MixAnalysisArgs(1);
  unknown[EncodableValue("source")] = EncodableValue("android_output_v1");
  EXPECT_EQ(Call(plugin, "startAnalysis", unknown).error_code, "unsupported_analysis_source");
}

// Opens the default render endpoint; skipped on machines without audio output.
TEST(ShipglowsAudioPlugin, SynthSessionPlaysAndFeedsMixAnalysis) {
  ShipglowsAudioPlugin plugin;
  auto start = SynthArgs(7);
  start[EncodableValue("instrument")] = EncodableValue("drums");
  const auto started = Call(plugin, "startSynth", start);
  if (!started.success && IsDeviceFailure(started.error_code)) GTEST_SKIP() << started.error_code;
  ASSERT_TRUE(started.success) << started.error_code;
  EXPECT_EQ(std::get<std::string>(Field(started, "state")), "running");
  EXPECT_TRUE(Call(plugin, "startSynth", start).success) << "same generation is idempotent";

  const auto lease = Call(plugin, "startAnalysis", MixAnalysisArgs(7));
  ASSERT_TRUE(lease.success) << lease.error_code;
  const auto lease_id = std::get<std::string>(Field(lease, "lease_id"));

  auto kick = SynthArgs(7);
  kick[EncodableValue("midi")] = EncodableValue(36);
  kick[EncodableValue("velocity")] = EncodableValue(0.9);
  for (int i = 0; i < 4; ++i) {
    const auto posted = Call(plugin, "noteOn", kick);
    ASSERT_TRUE(posted.success);
    EXPECT_TRUE(std::get<bool>(posted.value));
    Sleep(60);
  }
  auto stale = kick;
  stale[EncodableValue("generation")] = EncodableValue(int64_t{6});
  EXPECT_EQ(Call(plugin, "noteOn", stale).error_code, "stale_synth_generation");

  int64_t records = 0;
  float peak = 0.0F;
  int64_t cursor = 0;
  for (int poll = 0; poll < 40 && records < 16; ++poll) {
    Sleep(30);
    EncodableMap batch_args{{EncodableValue("analysisApiVersion"), EncodableValue(2)},
                            {EncodableValue("generation"), EncodableValue(int64_t{7})},
                            {EncodableValue("leaseId"), EncodableValue(lease_id)},
                            {EncodableValue("afterSequence"), EncodableValue(cursor)}};
    const auto batch = Call(plugin, "getAnalysisBatch", batch_args);
    ASSERT_TRUE(batch.success) << batch.error_code;
    const auto count = IntField(Field(batch, "record_count"));
    const auto& bytes = std::get<std::vector<uint8_t>>(Field(batch, "records"));
    ASSERT_EQ(bytes.size(), static_cast<size_t>(count) * 6492);
    for (int64_t r = 0; r < count; ++r) {
      float combined_peak = 0.0F;
      std::memcpy(&combined_peak, bytes.data() + r * 6492 + 40 + 2 * 4, sizeof(float));
      peak = std::max(peak, combined_peak);
    }
    records += count;
    cursor = IntField(Field(batch, "next_sequence_cursor"));
  }
  EXPECT_GE(records, 16) << "mix analysis publishes 10 ms records";
  EXPECT_GT(peak, 0.05F) << "analysis observes the synthesized kick";

  const auto status = Call(plugin, "getSynthStatus", SynthArgs(7));
  ASSERT_TRUE(status.success);
  EXPECT_GE(IntField(Field(status, "eventsApplied")), 4);
  EXPECT_GE(IntField(std::get<EncodableMap>(Field(status, "queueLatency"))
                         .at(EncodableValue("count"))), 5);

  const auto stopped = Call(plugin, "stopSynth", SynthArgs(7));
  ASSERT_TRUE(stopped.success);
  EXPECT_EQ(std::get<std::string>(Field(stopped, "state")), "stopped");
  EXPECT_EQ(Call(plugin, "noteOn", kick).error_code, "stale_synth_generation");
  EXPECT_EQ(Call(plugin, "startSynth", start).error_code, "stale_synth_generation");
}

// Real file playback through the shared bus while a synth session starts,
// plays and stops: playback keeps running without starvation.
TEST(ShipglowsAudioPlugin, SynthMixesWithFilePlayback) {
  ShipglowsAudioPlugin plugin;
  const auto wav = WriteToneWav(3.0);
  auto load = PlaybackArgs(1);
  load[EncodableValue("localFilePath")] = EncodableValue(wav.string());
  load[EncodableValue("seekSeconds")] = EncodableValue(0.0);
  load[EncodableValue("playbackSpeed")] = EncodableValue(1.0);
  load[EncodableValue("effects")] = EncodableValue(EncodableMap{});
  const auto loaded = Call(plugin, "loadPlayback", load);
  ASSERT_TRUE(loaded.success) << loaded.error_code;
  const auto played = Call(plugin, "playPlayback", PlaybackArgs(1));
  if (!played.success && IsDeviceFailure(played.error_code)) GTEST_SKIP() << played.error_code;
  ASSERT_TRUE(played.success) << played.error_code;
  Sleep(300);
  const auto before = Call(plugin, "getPlaybackStatus", PlaybackArgs(1));
  ASSERT_TRUE(before.success) << before.error_code;
  const auto position_before = std::get<double>(Field(before, "positionSeconds"));
  const auto wall_before = std::chrono::steady_clock::now();

  auto start = SynthArgs(3);
  start[EncodableValue("instrument")] = EncodableValue("piano");
  const auto started = Call(plugin, "startSynth", start);
  ASSERT_TRUE(started.success) << started.error_code;
  EXPECT_TRUE(std::get<bool>(Field(started, "playbackAttached")));
  auto note = SynthArgs(3);
  note[EncodableValue("midi")] = EncodableValue(64);
  note[EncodableValue("velocity")] = EncodableValue(0.7);
  ASSERT_TRUE(Call(plugin, "noteOn", note).success);
  Sleep(200);
  auto off = SynthArgs(3);
  off[EncodableValue("midi")] = EncodableValue(64);
  ASSERT_TRUE(Call(plugin, "noteOff", off).success);
  Sleep(100);
  ASSERT_TRUE(Call(plugin, "stopSynth", SynthArgs(3)).success);
  Sleep(300);

  const auto status = Call(plugin, "getPlaybackStatus", PlaybackArgs(1));
  ASSERT_TRUE(status.success) << status.error_code;
  EXPECT_EQ(std::get<std::string>(Field(status, "state")), "playing");
  // The source keeps pace with wall time across the synth session.
  const auto wall_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - wall_before).count();
  EXPECT_GT(std::get<double>(Field(status, "positionSeconds")) - position_before,
            wall_seconds - 0.1);
  EXPECT_EQ(IntField(Field(status, "underruns")), 0);
  ASSERT_TRUE(Call(plugin, "stopPlayback", PlaybackArgs(2)).success);
  std::error_code ignored;
  std::filesystem::remove(wav, ignored);
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
  EXPECT_EQ(ClassifyWasapiRouteChange(true, true, false),
            WasapiRouteChange::selected_device_lost);
}

TEST(WindowsWasapiCapture, FollowsIntentionalDefaultChange) {
  EXPECT_EQ(ClassifyWasapiRouteChange(true, true, true),
            WasapiRouteChange::default_device_changed);
}

TEST(WindowsWasapiCapture, IgnoresUnchangedDefault) {
  EXPECT_EQ(ClassifyWasapiRouteChange(false, true, true),
            WasapiRouteChange::none);
}

TEST(WindowsWasapiCapture, DetectsInactiveExplicitEndpointWithoutDefaultChange) {
  EXPECT_EQ(ClassifyWasapiRouteChange(false, false, false),
            WasapiRouteChange::selected_device_lost);
}

TEST(WindowsWasapiCapture, KeepsActiveExplicitEndpointAfterDefaultChange) {
  EXPECT_EQ(ClassifyWasapiRouteChange(true, false, true),
            WasapiRouteChange::none);
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
