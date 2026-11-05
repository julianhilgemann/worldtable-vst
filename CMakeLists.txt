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
    source/PluginEditor.cpp
    source/params/Params.cpp
    source/data/WorldData.cpp
    source/data/WorldSources.cpp
    source/data/WorldFeed.cpp
    source/data/TrackBuilder.cpp
    source/sky/Astronomy.cpp
    source/sky/Observer.cpp
    source/sky/SlowSky.cpp
    source/sky/SkyTimeline.cpp
    source/sky/SkyModel.cpp
    source/sky/SkyRuntime.cpp
    source/sky/SkyFeed.cpp
    source/data/SkyDays.cpp
    source/transform/WavetableBuilder.cpp
    source/dsp/Voice.cpp
    source/dsp/Engine.cpp
    source/dsp/Voicing.cpp
    source/dsp/ResonatorBank.cpp
    source/dsp/Exciter.cpp
    source/dsp/SkySound.cpp
    source/dsp/fx/ShimmerReverb.cpp)

target_include_directories(WorldtableShared INTERFACE source)

target_compile_definitions(WorldtableShared INTERFACE
    WORLDTABLE_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=1                # WebView UI (POC, see spec/tasks/TP.1-poc-sound.md)
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Factory world: recorded API responses bundled so the synth plays offline.
juce_add_binary_data(WorldtableWorldData
    HEADER_NAME WorldtableWorldData.h
    NAMESPACE   WorldtableWorldData
    SOURCES
        resources/world/wavespec/46026.data_spec
        resources/world/wavespec/51201.data_spec
        resources/world/wavespec/44013.data_spec
        resources/world/wavespec/41010.data_spec
        resources/world/geomag/BOU.json
        resources/world/geomag/CMO.json)

# Web UI assets, served to the WebView from memory.
file(GLOB WORLDTABLE_UI_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ui/*.html
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ui/*.css
    ${CMAKE_CURRENT_SOURCE_DIR}/source/ui/*.js)
# The recorded sky days (resources/sky/<day>/track.json), bundled as "<day>.json" so the names are
# unique (data/SkyDays.cpp looks them up by that name).
set(WORLDTABLE_SKY_DAYS 2024-05-10 2025-01-19 2025-01-25)
set(WORLDTABLE_SKY_FILES)
foreach(day IN LISTS WORLDTABLE_SKY_DAYS)
    configure_file(resources/sky/${day}/track.json ${CMAKE_CURRENT_BINARY_DIR}/skydata/${day}.json COPYONLY)
    list(APPEND WORLDTABLE_SKY_FILES ${CMAKE_CURRENT_BINARY_DIR}/skydata/${day}.json)
endforeach()

juce_add_binary_data(WorldtableSkyData
    HEADER_NAME WorldtableSkyData.h
    NAMESPACE   WorldtableSkyData
    SOURCES     ${WORLDTABLE_SKY_FILES})

juce_add_binary_data(WorldtableUIData
    HEADER_NAME WorldtableUIData.h
    NAMESPACE   WorldtableUIData
    SOURCES     ${WORLDTABLE_UI_FILES})

set_target_properties(WorldtableWorldData WorldtableSkyData WorldtableUIData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(WorldtableShared INTERFACE
    WorldtableWorldData
    WorldtableSkyData
    WorldtableUIData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_link_libraries(Worldtable PRIVATE WorldtableShared)

#------------------------------------------------------------------------------
# Offline renderer: headless processor → WAV (spec T1.5, pulled forward for the POC)
#------------------------------------------------------------------------------
juce_add_console_app(WorldtableRender PRODUCT_NAME "WorldtableRender")
target_sources(WorldtableRender PRIVATE tools/render/RenderOffline.cpp)
target_link_libraries(WorldtableRender PRIVATE WorldtableShared)

# UI integration check: real editor + WebView, bridge verified over evaluateJavascript.
juce_add_gui_app(WorldtableUICheck PRODUCT_NAME "WorldtableUICheck")
target_sources(WorldtableUICheck PRIVATE tools/uicheck/UiCheck.cpp)
target_compile_definitions(WorldtableUICheck PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(WorldtableUICheck PRIVATE WorldtableShared)

#------------------------------------------------------------------------------
# Tests
#------------------------------------------------------------------------------
if(WORLDTABLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
