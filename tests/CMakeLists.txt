add_executable(shipglows_audio_engine_tests
  engine_tests.cpp
)

target_compile_features(shipglows_audio_engine_tests PRIVATE cxx_std_20)
target_link_libraries(shipglows_audio_engine_tests PRIVATE shipglows::audio_engine)

add_test(NAME shipglows_audio_engine_tests COMMAND shipglows_audio_engine_tests)

add_executable(shipglows_visual_analysis_tests
  ../flutter/shipglows_audio/native/engine/tests/visual_analysis_test.cpp)
target_compile_features(shipglows_visual_analysis_tests PRIVATE cxx_std_20)
target_link_libraries(shipglows_visual_analysis_tests PRIVATE shipglows::audio_engine)
add_test(NAME shipglows_visual_analysis_tests COMMAND shipglows_visual_analysis_tests)

add_executable(shipglows_offline_wav_tests
  ../flutter/shipglows_audio/native/engine/tests/offline_wav_analysis_test.cpp)
target_compile_features(shipglows_offline_wav_tests PRIVATE cxx_std_20)
target_link_libraries(shipglows_offline_wav_tests PRIVATE shipglows::audio_engine)
add_test(NAME shipglows_offline_wav_tests COMMAND shipglows_offline_wav_tests)

add_executable(shipglows_offline_sidecar_tests
  ../flutter/shipglows_audio/native/engine/tests/offline_sidecar_test.cpp)
target_compile_features(shipglows_offline_sidecar_tests PRIVATE cxx_std_20)
target_link_libraries(shipglows_offline_sidecar_tests PRIVATE shipglows::audio_engine)
add_test(NAME shipglows_offline_sidecar_tests COMMAND shipglows_offline_sidecar_tests)

add_executable(shipglows_audio_offline_job_tests
  ../tools/audio_analysis_cli/offline_job_test.cpp
  ../tools/audio_analysis_cli/offline_job.cpp)
target_compile_features(shipglows_audio_offline_job_tests PRIVATE cxx_std_20)
target_include_directories(shipglows_audio_offline_job_tests PRIVATE
  ../tools/audio_analysis_cli)
target_link_libraries(shipglows_audio_offline_job_tests PRIVATE shipglows::audio_engine)
add_test(NAME shipglows_audio_offline_job_tests
         COMMAND shipglows_audio_offline_job_tests)
if(WIN32)
  find_program(SHIPGLOWS_POWERSHELL_EXECUTABLE powershell.exe)
  if(SHIPGLOWS_POWERSHELL_EXECUTABLE)
    add_test(NAME shipglows_audio_analysis_cli_smoke
      COMMAND "${SHIPGLOWS_POWERSHELL_EXECUTABLE}" -NoProfile
        -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/audio_analysis_cli/cli_smoke.ps1"
        -CliPath "$<TARGET_FILE:shipglows_audio_analysis_cli>")
  endif()
endif()

add_executable(shipglows_audio_interruption_bench interruption_bench.cpp)
target_link_libraries(shipglows_audio_interruption_bench PRIVATE shipglows_audio_engine)
target_compile_features(shipglows_audio_interruption_bench PRIVATE cxx_std_20)
target_include_directories(shipglows_audio_interruption_bench PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../flutter/shipglows_audio/android/src/main/cpp"
)
add_test(NAME shipglows_audio_interruption_bench
         COMMAND shipglows_audio_interruption_bench)

if(WIN32)
  add_executable(shipglows_audio_sources_tests
    windows_sources_tests.cpp
    ../flutter/shipglows_audio/windows/windows_wasapi_capture.cpp
    ../flutter/shipglows_audio/windows/windows_wasapi_sources.cpp)
  target_compile_features(shipglows_audio_sources_tests PRIVATE cxx_std_20)
  target_compile_definitions(shipglows_audio_sources_tests PRIVATE NOMINMAX UNICODE _UNICODE)
  target_include_directories(shipglows_audio_sources_tests PRIVATE ../flutter/shipglows_audio/windows)
  target_link_libraries(shipglows_audio_sources_tests PRIVATE shipglows::audio_engine ole32 avrt mmdevapi)
  add_test(NAME shipglows_audio_sources_tests COMMAND shipglows_audio_sources_tests)
endif()
