juce_add_console_app(WorldtableTests PRODUCT_NAME "WorldtableTests")

target_sources(WorldtableTests PRIVATE
    unit/PluginProcessorTests.cpp
    unit/UiSettingsTests.cpp
    unit/WorldDataTests.cpp
    unit/WavetableBuilderTests.cpp
    unit/DspUnitTests.cpp
    unit/SkyInterfaceTests.cpp
    unit/SkySlowTests.cpp
    unit/SkyGoldenTests.cpp
    unit/SkyModelTests.cpp
    unit/SkyProjectionTests.cpp
    unit/SkyRuntimeTests.cpp
    unit/HarmonyTests.cpp
    unit/FxTests.cpp
    unit/SkySoundTests.cpp
    unit/TrackBuilderTests.cpp
    unit/LowTests.cpp
    unit/PlayModeTests.cpp
    unit/BandTests.cpp
    unit/EqTests.cpp
    unit/AllocationCounter.cpp)

target_compile_definitions(WorldtableTests PRIVATE
    WORLDTABLE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    WORLDTABLE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    WORLDTABLE_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources")

target_link_libraries(WorldtableTests PRIVATE
    WorldtableShared
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(WorldtableTests DISCOVERY_MODE PRE_TEST)
