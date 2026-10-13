# Standalone decoder trials: no Flutter, no engine dependency.
#   cmake -S tests/decoder_trials -B build/decoder-trials -G "Visual Studio 17 2022" -A x64
#   cmake --build build/decoder-trials --config Release
#   ctest --test-dir build/decoder-trials -C Release --output-on-failure
cmake_minimum_required(VERSION 3.22)

project(shipglows_decoder_trials VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "shipglows_decoder_trials targets Windows Media Foundation only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SHIPGLOWS_DECODER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../flutter/shipglows_audio/windows")
set(SHIPGLOWS_FIXTURE_DIR "${CMAKE_BINARY_DIR}/fixtures" CACHE PATH
    "Directory for generated synthetic decoder fixtures (git-ignored under build/)")

add_library(shipglows_mf_decoder STATIC "${SHIPGLOWS_DECODER_DIR}/windows_mf_decoder.cpp")
target_include_directories(shipglows_mf_decoder PUBLIC "${SHIPGLOWS_DECODER_DIR}")
target_compile_definitions(shipglows_mf_decoder PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(shipglows_mf_decoder PUBLIC mfplat mfreadwrite mfuuid ole32)

add_executable(shipglows_fixture_gen fixture_gen.cpp)
target_include_directories(shipglows_fixture_gen PRIVATE "${SHIPGLOWS_DECODER_DIR}")
target_compile_definitions(shipglows_fixture_gen PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(shipglows_fixture_gen PRIVATE mfplat mfreadwrite mfuuid ole32)

add_executable(shipglows_decoder_trials decoder_trials.cpp)
target_compile_definitions(shipglows_decoder_trials PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(shipglows_decoder_trials PRIVATE shipglows_mf_decoder ole32)

enable_testing()
add_test(NAME decoder_fixtures_generate
         COMMAND shipglows_fixture_gen "${SHIPGLOWS_FIXTURE_DIR}")
add_test(NAME decoder_trials
         COMMAND shipglows_decoder_trials "${SHIPGLOWS_FIXTURE_DIR}")
set_tests_properties(decoder_fixtures_generate PROPERTIES FIXTURES_SETUP shipglows_decoder_fixtures)
set_tests_properties(decoder_trials PROPERTIES FIXTURES_REQUIRED shipglows_decoder_fixtures)
