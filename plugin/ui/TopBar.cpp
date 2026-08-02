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

#include "TopBar.h"

namespace blockwave::ui
{

TopBar::TopBar (BlockwaveAudioProcessor& processor) : proc (processor)
{
    setOpaque (true);

    prevBtn.setTooltip ("previous preset");
    prevBtn.onClick = [this] { stepPreset (false); };
    addAndMakeVisible (prevBtn);

    nextBtn.setTooltip ("next preset");
    nextBtn.onClick = [this] { stepPreset (true); };
    addAndMakeVisible (nextBtn);

    nameBtn.setComponentID ("namebox");
    nameBtn.setTooltip ("preset browser");
    nameBtn.onClick = [this] { if (onBrowse) onBrowse(); };
    addAndMakeVisible (nameBtn);

    browseBtn.setTooltip ("preset browser");
    browseBtn.onClick = [this] { if (onBrowse) onBrowse(); };
    addAndMakeVisible (browseBtn);

    saveBtn.setTooltip ("save user preset");
    saveBtn.onClick = [this] { if (onSave) onSave(); };
    addAndMakeVisible (saveBtn);

    rawBtn.setComponentID ("led");
    rawBtn.setTooltip ("aliased retro dirt");
    addAndMakeVisible (rawBtn);
    rawAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "raw", rawBtn);

    masterKnob.setTooltip ("final output level");
    masterKnob.setWantsKeyboardFocus (true);
    masterKnob.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (masterKnob);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, "master_gain", masterKnob);

    scaleBtn.setTooltip ("pixel size cycle");
    scaleBtn.onClick = [this]
    {
        // 100 -> 125 -> 150 -> 175 -> 200 -> 100.
        if (onScaleChange)
            onScaleChange (currentScale >= 200 ? 100 : currentScale + 25);
    };
    addAndMakeVisible (scaleBtn);

    refresh();
}

void TopBar::stepPreset (bool forward)
{
    juce::String err;
    if (forward)
        proc.loadNextPreset (err);
    else
        proc.loadPrevPreset (err);
    refresh();
    if (onPresetChanged)
        onPresetChanged();
}

void TopBar::refresh()
{
    const auto name = proc.getPresetName();
    const auto cat  = proc.getPresetCategory();
    nameBtn.setButtonText (cat.isNotEmpty() ? cat + " : " + name : name);
    repaint();
}

void TopBar::setScaleLabel (int s)
{
    currentScale = s;
    scaleBtn.setButtonText (juce::String (s) + "%");   // shows the CURRENT scale
}

void TopBar::paint (juce::Graphics& g)
{
    using namespace colours;
    g.fillAll (night);

    // Logo, bitmap font at 2x with a 1-px night shadow: BLOCK grass, WAVE ice.
    const int ly = (kTopBarH - 12) / 2;
    drawPixelText (g, "BLOCK", 9, ly + 1, 2, outline);
    const int lw = drawPixelText (g, "BLOCK", 8, ly, 2, grass);
    drawPixelText (g, "WAVE", 8 + lw + 1, ly + 1, 2, outline);
    drawPixelText (g, "WAVE", 8 + lw, ly, 2, ice);

    // Lava accent line under the bar.
    g.setColour (lava);
    g.fillRect (0, kTopBarH - 2, getWidth(), 2);
}

void TopBar::resized()
{
    prevBtn.setBounds   (88,  12, 16, 16);
    nameBtn.setBounds   (108, 12, 168, 16);
    nextBtn.setBounds   (280, 12, 16, 16);
    browseBtn.setBounds (304, 12, 64, 16);
    saveBtn.setBounds   (376, 12, 48, 16);
    rawBtn.setBounds    (648, 12, 56, 16);
    masterKnob.setBounds (712, 8, 24, 24);
    scaleBtn.setBounds  (784, 12, 40, 16);             // fits "100%" at 1x
}

} // namespace blockwave::ui
