#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace worldtable
{

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p)
{
    setResizable (true, true);
    setResizeLimits (800, 512, 2000, 1280);
    setSize (1000, 640); // spec/05-ui-mock.md
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101214));

    auto area = getLocalBounds().withSizeKeepingCentre (getWidth(), 80);

    g.setColour (juce::Colour (0xffc8ccd0));
    g.setFont (juce::FontOptions (28.0f));
    g.drawText ("WORLDTABLE", area.removeFromTop (44), juce::Justification::centred);

    g.setColour (juce::Colour (0xff6c7378));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("M0 skeleton - silence", area, juce::Justification::centred);
}

} // namespace worldtable
