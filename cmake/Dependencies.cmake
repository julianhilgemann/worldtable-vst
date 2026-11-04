# Third-party dependencies, pinned. Bump deliberately, in its own PR.
# Approved in ADR-0001 (JUCE, Catch2). Ask before adding anything here.
include(FetchContent)

set(WORLDTABLE_JUCE_TAG   "8.0.17-security")
set(WORLDTABLE_CATCH2_TAG "v3.16.1")

# Tip: point at a local checkout to skip the download, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${WORLDTABLE_JUCE_TAG}
    GIT_SHALLOW    TRUE
    SYSTEM)
FetchContent_MakeAvailable(JUCE)

if(WORLDTABLE_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        ${WORLDTABLE_CATCH2_TAG}
        GIT_SHALLOW    TRUE
        SYSTEM)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
