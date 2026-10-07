cmake_minimum_required(VERSION 3.20)
project(shipglows_audio_dsp_tests LANGUAGES CXX)

enable_testing()

add_executable(dsp_chain_test
  dsp_chain_test.cpp
  ../src/dsp_chain.cpp
)
target_include_directories(dsp_chain_test PRIVATE ../include)
target_compile_features(dsp_chain_test PRIVATE cxx_std_17)
add_test(NAME dsp_chain_test COMMAND dsp_chain_test)
