cmake_minimum_required(VERSION 3.22.1)

project(shipglows_audio_android LANGUAGES CXX)

find_package(oboe REQUIRED CONFIG)

if(NOT TARGET shipglows_audio_engine)
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../native/engine"
    "${CMAKE_CURRENT_BINARY_DIR}/shipglows_audio_engine"
  )
endif()

add_library(shipglows_audio_jni SHARED
  android_oboe_capture.cpp
  android_oboe_capture.h
  jni_bridge.cpp
)

target_compile_features(shipglows_audio_jni PRIVATE cxx_std_20)
target_compile_options(shipglows_audio_jni PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
)
target_link_libraries(shipglows_audio_jni PRIVATE
  android
  log
  oboe::oboe
  shipglows::audio_engine
)
