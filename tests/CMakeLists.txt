add_executable(shipglows_audio_engine_tests
  engine_tests.cpp
)

target_compile_features(shipglows_audio_engine_tests PRIVATE cxx_std_20)
target_link_libraries(shipglows_audio_engine_tests PRIVATE shipglows::audio_engine)

add_test(NAME shipglows_audio_engine_tests COMMAND shipglows_audio_engine_tests)

add_executable(shipglows_audio_interruption_bench interruption_bench.cpp)
target_link_libraries(shipglows_audio_interruption_bench PRIVATE shipglows_audio_engine)
target_compile_features(shipglows_audio_interruption_bench PRIVATE cxx_std_20)
target_include_directories(shipglows_audio_interruption_bench PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../flutter/shipglows_audio/android/src/main/cpp"
)
add_test(NAME shipglows_audio_interruption_bench
         COMMAND shipglows_audio_interruption_bench)
