cmake_minimum_required(VERSION 3.22)
project(Reverso VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Reverso
  COMPANY_NAME "Reverso Audio"
  PLUGIN_MANUFACTURER_CODE Rvso
  PLUGIN_CODE Rvrs
  FORMATS VST3
  PRODUCT_NAME "Reverso"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx Delay
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Reverso PRIVATE src/PluginProcessor.cpp src/dsp.h)
target_include_directories(Reverso PRIVATE src)
target_compile_definitions(Reverso PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Reverso PRIVATE juce::juce_audio_processors)
