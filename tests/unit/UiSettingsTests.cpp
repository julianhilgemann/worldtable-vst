// T6.13: the editor's per-computer settings: three sizes, the size it opens at, the file that keeps
// the choice and whether the welcome tour was seen.
#include "PluginEditor.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using worldtable::UiSettings;

TEST_CASE ("UiSettings: the three sizes are the 1350 x 900 page at 85, 100 and 125 %", "[ui][T6.13]")
{
    CHECK (UiSettings::sizeOf (UiSettings::kSmall) == juce::Point<int> (1148, 765));
    CHECK (UiSettings::sizeOf (UiSettings::kMedium) == juce::Point<int> (1350, 900));
    CHECK (UiSettings::sizeOf (UiSettings::kLarge) == juce::Point<int> (1688, 1125));
    for (int i = 0; i < UiSettings::kNumSizes; ++i)
    {
        const auto s = UiSettings::sizeOf (i);
        const double k = static_cast<double> (s.y) / 900.0;   // the page fits the window: 3:2 within a pixel
        CHECK (std::abs (s.x - 1350.0 * k) < 1.0);
    }
}

TEST_CASE ("UiSettings: an editor opens at the saved size, else Medium, Small on a small display", "[ui][T6.13]")
{
    const juce::Rectangle<int> fullHd { 0, 0, 1920, 1055 }, laptop { 0, 25, 1440, 875 };
    CHECK (UiSettings::openingSize (-1, fullHd) == UiSettings::kMedium);
    CHECK (UiSettings::openingSize (-1, laptop) == UiSettings::kSmall);
    // the margin is 80 px on both sides of Medium
    CHECK (UiSettings::openingSize (-1, { 0, 0, 1430, 980 }) == UiSettings::kMedium);
    CHECK (UiSettings::openingSize (-1, { 0, 0, 1429, 980 }) == UiSettings::kSmall);
    CHECK (UiSettings::openingSize (-1, { 0, 0, 1430, 979 }) == UiSettings::kSmall);
    // a choice once made is kept, whatever the display
    CHECK (UiSettings::openingSize (UiSettings::kLarge, laptop) == UiSettings::kLarge);
    CHECK (UiSettings::openingSize (UiSettings::kSmall, fullHd) == UiSettings::kSmall);
    // nonsense in the file reads as no choice
    CHECK (UiSettings::openingSize (7, fullHd) == UiSettings::kMedium);
}

TEST_CASE ("UiSettings: the file keeps the size and the tour for every editor that opens later", "[ui][T6.13]")
{
    const juce::ScopedJuceInitialiser_GUI juce;   // a PropertiesFile is a ChangeBroadcaster (message thread)
    const juce::TemporaryFile tmp (".settings");
    const auto file = tmp.getFile();
    UiSettings settings (file);
    CHECK (settings.size() == -1);   // a clean file: no choice, the tour not seen
    CHECK_FALSE (settings.tourSeen());

    settings.setSize (UiSettings::kLarge);
    CHECK (UiSettings (file).size() == UiSettings::kLarge);   // a new reader sees it at once
    settings.setTourSeen (true);
    CHECK (UiSettings (file).tourSeen());
    CHECK (UiSettings (file).size() == UiSettings::kLarge);   // one key does not lose the other

    settings.setSize (9);   // out of range: clamped
    CHECK (settings.size() == UiSettings::kLarge);
    settings.setSize (-3);
    CHECK (settings.size() == UiSettings::kSmall);

    REQUIRE (file.replaceWithText ("not a settings file"));   // a damaged file reads as defaults
    CHECK (settings.size() == -1);
    CHECK_FALSE (settings.tourSeen());
}

#if ! JUCE_WINDOWS
TEST_CASE ("UiSettings: $WORLDTABLE_SETTINGS_FILE replaces the person's own file", "[ui][T6.13]")
{
    const auto before = juce::SystemStats::getEnvironmentVariable ("WORLDTABLE_SETTINGS_FILE", {});
    unsetenv ("WORLDTABLE_SETTINGS_FILE");
    const auto own = UiSettings::defaultFile();
    CHECK (own.getFileName() == "Worldtable.settings");
    CHECK (own.getParentDirectory().getFileName() == "Worldtable");

    const juce::TemporaryFile tmp (".settings");
    setenv ("WORLDTABLE_SETTINGS_FILE", tmp.getFile().getFullPathName().toRawUTF8(), 1);
    CHECK (UiSettings::defaultFile() == tmp.getFile());
    CHECK (UiSettings().getFile() == tmp.getFile());

    if (before.isNotEmpty())
        setenv ("WORLDTABLE_SETTINGS_FILE", before.toRawUTF8(), 1);
    else
        unsetenv ("WORLDTABLE_SETTINGS_FILE");
}
#endif
