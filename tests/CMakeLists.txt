juce_add_console_app(WorldtableTests PRODUCT_NAME "WorldtableTests")

target_sources(WorldtableTests PRIVATE
    unit/PluginProcessorTests.cpp
    unit/WorldDataTests.cpp
    unit/WavetableBuilderTests.cpp
    unit/DspUnitTests.cpp)

target_compile_definitions(WorldtableTests PRIVATE
    WORLDTABLE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

target_link_libraries(WorldtableTests PRIVATE
    WorldtableShared
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(WorldtableTests DISCOVERY_MODE PRE_TEST)
