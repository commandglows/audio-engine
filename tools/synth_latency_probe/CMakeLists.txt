# Device probe: plays clicks on the default endpoint, so it is not a CTest test.
add_executable(shipglows_synth_latency_probe
  synth_latency_probe.cpp
  ../../flutter/shipglows_audio/windows/windows_output_bus.cpp
  ../../flutter/shipglows_audio/windows/windows_wasapi_playback.cpp)
target_compile_features(shipglows_synth_latency_probe PRIVATE cxx_std_20)
target_compile_definitions(shipglows_synth_latency_probe PRIVATE NOMINMAX UNICODE _UNICODE)
target_include_directories(shipglows_synth_latency_probe PRIVATE
  ../../flutter/shipglows_audio/windows)
target_link_libraries(shipglows_synth_latency_probe PRIVATE
  shipglows::audio_engine ole32 avrt mmdevapi)
if(MSVC)
  target_compile_options(shipglows_synth_latency_probe PRIVATE /W4 /wd4324)
endif()
