juce_add_console_app(WorldtableTests PRODUCT_NAME "WorldtableTests")

target_sources(WorldtableTests PRIVATE
    unit/PluginProcessorTests.cpp)

target_link_libraries(WorldtableTests PRIVATE
    WorldtableShared
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(WorldtableTests DISCOVERY_MODE PRE_TEST)
