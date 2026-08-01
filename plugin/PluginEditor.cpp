/*
    BLOCKWAVE — square-wave-only synthesizer
    Copyright (C) 2026 Kirill Boyko

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "PluginEditor.h"

BlockwaveAudioProcessorEditor::BlockwaveAudioProcessorEditor (BlockwaveAudioProcessor& p)
    : AudioProcessorEditor (p)
{
    // Placeholder canvas at the target fixed pixel size (1x). Real pixel-art
    // UI lands in Phase 3.
    setSize (640, 360);
}

void BlockwaveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1c1c24));           // "night" from SPEC palette
    g.setColour (juce::Colour (0xff8b8b8b));         // "stone gray"
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText (JucePlugin_Name, getLocalBounds(), juce::Justification::centred);
    g.setColour (juce::Colour (0xff5cab3f));         // "grass green"
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Phase 0 shell — engine coming in Phase 1",
                getLocalBounds().removeFromBottom (40), juce::Justification::centred);
}
