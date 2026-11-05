#pragma once

#include <juce_core/juce_core.h>

#ifndef WORLDTABLE_FIXTURES_DIR
 #error "WORLDTABLE_FIXTURES_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace testing
{
inline juce::File fixture (const juce::String& relative)
{
    return juce::File (WORLDTABLE_FIXTURES_DIR).getChildFile (relative);
}

inline juce::String loadFixture (const juce::String& relative)
{
    const auto f = fixture (relative);
    jassert (f.existsAsFile());
    return f.loadFileAsString();
}
} // namespace testing
