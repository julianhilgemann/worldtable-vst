cmake_minimum_required(VERSION 3.25)

# Must precede project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Worldtable VERSION 0.1.0)

# JUCE's post-build steps (VST3 moduleinfo, copy, signing) are not shell-escaped:
# a build path containing e.g. '&' breaks them with cryptic /bin/sh errors.
if(CMAKE_BINARY_DIR MATCHES "[&;|<>()$`]")
    message(FATAL_ERROR
        "Build directory '${CMAKE_BINARY_DIR}' contains shell metacharacters, "
        "which break JUCE's post-build steps. Use a build directory elsewhere "
        "(see 'Building locally' in spec/tasks/T0.1-skeleton.md).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(WORLDTABLE_BUILD_TESTS  "Build the Catch2 unit tests" ON)
option(WORLDTABLE_COPY_PLUGINS "Copy built plugins into the user plug-in folders" OFF)

include(cmake/Dependencies.cmake)

#------------------------------------------------------------------------------
# Plugin
#------------------------------------------------------------------------------
set(WORLDTABLE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND WORLDTABLE_FORMATS AU)
endif()

# Identity values are PLACEHOLDERS until first public release; after that they
# are permanent (DAW sessions + AU registration). See spec/tasks/T0.1-skeleton.md.
juce_add_plugin(Worldtable
    PRODUCT_NAME             "Worldtable"
    COMPANY_NAME             "Worldtable"
    BUNDLE_ID                com.worldtable.worldtable
    PLUGIN_MANUFACTURER_CODE Wrld
    PLUGIN_CODE              Wtbl
    FORMATS                  ${WORLDTABLE_FORMATS}
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES          Instrument Synth
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD  ${WORLDTABLE_COPY_PLUGINS})

# Everything except the format wrappers lives in this interface target, so the
# unit tests compile exactly the same sources and flags as the plugin.
add_library(WorldtableShared INTERFACE)

target_sources(WorldtableShared INTERFACE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)

target_include_directories(WorldtableShared INTERFACE source)

target_compile_definitions(WorldtableShared INTERFACE
    JUCE_WEB_BROWSER=0                # WebView UI arrives with ADR-0004 / T5.1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(WorldtableShared INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_link_libraries(Worldtable PRIVATE WorldtableShared)

#------------------------------------------------------------------------------
# Tests
#------------------------------------------------------------------------------
if(WORLDTABLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
