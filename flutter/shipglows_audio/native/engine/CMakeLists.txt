add_library(shipglows_audio_engine STATIC
  src/audio_lifecycle.cpp
  src/audio_device_manager.cpp
  src/capture_session.cpp
  src/dsp_chain.cpp
  src/analysis_stream.cpp
  src/offline_wav_analysis.cpp
  src/offline_sidecar.cpp
  src/visual_transform_contract.cpp
  src/pcm_analysis.cpp
  src/recording_preflight.cpp
  src/segmented_pcm_store.cpp
  src/segmented_wav_store.cpp
  src/sha256.cpp
  src/synth_engine.cpp
)

add_library(shipglows::audio_engine ALIAS shipglows_audio_engine)

target_compile_features(shipglows_audio_engine PUBLIC cxx_std_20)
target_include_directories(shipglows_audio_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(MSVC)
  # C4324: intentional padding from alignas(64) on queue atomics (false-sharing guard).
  target_compile_options(shipglows_audio_engine PRIVATE /W4 /permissive- /wd4324)
else()
  target_compile_options(shipglows_audio_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(shipglows_audio_engine PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
