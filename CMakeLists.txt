cmake_minimum_required(VERSION 3.22)

project(shipglows_audio_engine VERSION 0.1.0 LANGUAGES CXX)

option(SHIPGLOWS_AUDIO_BUILD_TESTS "Build native engine tests" ON)

add_subdirectory(flutter/shipglows_audio/native/engine)
add_subdirectory(tools/audio_analysis_cli)
if(WIN32)
  add_subdirectory(tools/synth_latency_probe)
endif()

if(SHIPGLOWS_AUDIO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
