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

#pragma once

// Always-visible top bar (SPEC §UI): logo, preset prev/next, preset name
// (click opens the browser), PRESETS, SAVE, RAW LED toggle, master volume
// knob, 1x/2x scale toggle. Preset loading runs on the message thread and
// lands as atomic parameter writes (via the processor's preset API).

#include "../PluginProcessor.h"
#include "ParamCells.h"

namespace blockwave::ui
{

class TopBar final : public juce::Component
{
public:
    explicit TopBar (BlockwaveAudioProcessor& processor);

    std::function<void()> onBrowse, onSave, onPresetChanged;
    std::function<void (int)> onScaleChange;                 // 1 or 2

    void refresh();                                          // preset name/cat
    void setScaleLabel (int currentScale);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void stepPreset (bool forward);

    BlockwaveAudioProcessor& proc;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, nameBtn,
                     browseBtn { "PRESETS" }, saveBtn { "SAVE" },
                     scaleBtn { "2X" };
    juce::ToggleButton rawBtn { "RAW" };
    PixelSlider masterKnob { juce::Slider::RotaryVerticalDrag,
                             juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rawAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAtt;
    int currentScale = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};

} // namespace blockwave::ui
