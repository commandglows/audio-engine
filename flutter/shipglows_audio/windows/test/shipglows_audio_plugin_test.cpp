#include <flutter/method_call.h>
#include <flutter/method_result_functions.h>
#include <flutter/standard_method_codec.h>
#include <gtest/gtest.h>
#include <windows.h>

#include <memory>
#include <string>
#include <variant>

#include "shipglows_audio_plugin.h"

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

}  // namespace test
}  // namespace shipglows_audio
