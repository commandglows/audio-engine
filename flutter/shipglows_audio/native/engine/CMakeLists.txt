add_library(shipglows_audio_engine STATIC
  src/audio_device_manager.cpp
  src/capture_session.cpp
  src/pcm_analysis.cpp
  src/segmented_pcm_store.cpp
  src/segmented_wav_store.cpp
  src/sha256.cpp
)

add_library(shipglows::audio_engine ALIAS shipglows_audio_engine)

target_compile_features(shipglows_audio_engine PUBLIC cxx_std_20)
target_include_directories(shipglows_audio_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(MSVC)
  target_compile_options(shipglows_audio_engine PRIVATE /W4 /permissive-)
else()
  target_compile_options(shipglows_audio_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(shipglows_audio_engine PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
