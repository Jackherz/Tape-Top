cmake_minimum_required(VERSION 3.22)
project(TapeTop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

# ---------------------------------------------------------------------------
# JUCE 8, fetched at configure time so the repo does not vendor a framework.
#
# Licensing: JUCE 8 is dual licensed (AGPLv3 or the commercial JUCE licence).
# This project is personal/non-commercial, so either the free Personal tier or
# AGPLv3 applies. If you ever distribute it, pick deliberately -- see
# docs/02-legal.md.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(TapeTop
    PRODUCT_NAME              "Tape-Top"
    COMPANY_NAME              "Tape-Top"
    BUNDLE_ID                 "com.local.tapetop"
    PLUGIN_MANUFACTURER_CODE  Tptp
    PLUGIN_CODE               Tt08
    FORMATS                   Standalone AU VST3
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Tape-Top records audio onto its tape.")

target_sources(TapeTop PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(TapeTop PRIVATE
    Source
    ../dsp/include)      # tt::Deck / tt::TapeEditor, shared with the tests

target_compile_definitions(TapeTop PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TapeTop PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
