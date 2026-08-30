cmake_minimum_required(VERSION 3.22)

project(shipglows_audio_engine VERSION 0.1.0 LANGUAGES CXX)

option(SHIPGLOWS_AUDIO_BUILD_TESTS "Build native engine tests" ON)

add_subdirectory(flutter/shipglows_audio/native/engine)

if(SHIPGLOWS_AUDIO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
