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

// One 48x56 "cell" per parameter: bitmap-font label on top, control in the
// middle (stepped pixel knob / block toggle / choice cycler), bitmap-font
// value readout on an LCD-style chip below. All parameter traffic goes
// through APVTS attachments — the UI thread never touches the audio thread.

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamSpec.h"
#include "PixelTheme.h"
#include "PixelFont.h"

namespace blockwave::ui
{

// Human-readable bitmap-font value ("12.5K", "1/16D", "+7", "ON"...).
// Needs the APVTS to resolve LFO sync state for synced-rate display.
juce::String formatParamValue (PId id, float plain,
                               const juce::AudioProcessorValueTreeState& state);

// Slider with explicit arrow-key stepping (keyboard reachability).
class PixelSlider final : public juce::Slider
{
public:
    using juce::Slider::Slider;
    bool keyPressed (const juce::KeyPress&) override;
};

// Click-to-cycle choice box (left/wheel/arrows = next, right-click = prev).
// Deliberately not a native ComboBox: no OS popup can look like pixel art.
class PixelChoice final : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    PixelChoice (juce::RangedAudioParameter& param, const ParamDef& def);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void cycle (int delta);
    const ParamDef& def;
    int index = 0;
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelChoice)
};

class ParamCell final : public juce::Component
{
public:
    static constexpr int cellW = kCellW;
    static constexpr int cellH = kCellH;

    ParamCell (juce::AudioProcessorValueTreeState& state, PId pid,
               const juce::String& label, const juce::String& tooltip);

    void refreshValue();                    // recompute readout, repaint chip
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::AudioProcessorValueTreeState& state;
    PId pid;
    juce::String labelText, valueText;
    bool hasValueChip = true;

    std::unique_ptr<PixelSlider> slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAtt;
    std::unique_ptr<PixelChoice> choice;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamCell)
};

} // namespace blockwave::ui
