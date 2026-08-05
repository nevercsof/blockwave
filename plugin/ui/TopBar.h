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
// (click opens the browser), PRESETS, SAVE, favourite star, RAW LED toggle,
// master volume knob, UI SCALE slider (100/125/150/175/200 %). Preset loading
// runs on the message thread and lands as atomic parameter writes (via the
// processor's preset API).
//
// The scale control is a 5-notch SLIDER, not the old cycling button
// (producer's call: the percent scheme stays, the interaction becomes direct —
// you can see all five stops and jump straight to one instead of clicking
// through four to get back). Layout: the bar had a 184 px hole between the
// star and RAW, so the sound group (RAW + master) moved left into it and the
// 200 px scale group took the right end — no control got smaller and every
// group is separated by 40 px of night.

#include "../PluginProcessor.h"
#include "ParamCells.h"

namespace blockwave::ui
{

// Horizontal pixel slider with 5 discrete notches and a percent readout.
// Not a juce::Slider: the value is UI-only (never a parameter), the steps are
// a fixed list, and the whole thing must be drawn as flat blocks.
class PixelScaleSlider final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    static constexpr int kNumSteps = 5;
    static constexpr int kStepPercent[kNumSteps] = { 100, 125, 150, 175, 200 };

    static constexpr int kNotchPitch = 32;
    static constexpr int kHandleW    = 16;
    static constexpr int kTrackW     = kNotchPitch * (kNumSteps - 1) + kHandleW;  // 144
    static constexpr int kReadoutW   = 48;
    static constexpr int width       = kTrackW + 8 + kReadoutW;                   // 200
    static constexpr int height      = 28;

    PixelScaleSlider();

    std::function<void (int)> onScaleChange;      // percent

    void setScalePercent (int percent);           // display only, no callback
    int getScalePercent() const noexcept { return kStepPercent[index]; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    static int indexForPercent (int percent) noexcept;
    juce::Rectangle<int> trackBounds() const;
    void setIndex (int newIndex, bool notify);
    void setFromMouse (int x);

    int index = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelScaleSlider)
};

// 16x16 star toggle: procedural pixel art, no LookAndFeel button chrome.
// Lit = the current preset is a favorite. Keyboard-reachable like every
// other top-bar control.
class StarButton final : public juce::Button
{
public:
    StarButton() : juce::Button ("FAVORITE") { setWantsKeyboardFocus (true); }
    void setLit (bool shouldBeLit)
    {
        if (shouldBeLit != lit) { lit = shouldBeLit; repaint(); }
    }
    bool isLit() const { return lit; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool lit = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StarButton)
};

class TopBar final : public juce::Component
{
public:
    explicit TopBar (BlockwaveAudioProcessor& processor);

    std::function<void()> onBrowse, onSave, onPresetChanged;
    std::function<void()> onFavoriteToggled;                 // browser count changed
    std::function<void (int)> onScaleChange;                 // percent: 100..200

    void refresh();                                          // preset name/cat
    void setScalePercent (int currentScalePercent);          // display only

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void stepPreset (bool forward);

    BlockwaveAudioProcessor& proc;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, nameBtn,
                     browseBtn { "PRESETS" }, saveBtn { "SAVE" };
    juce::ToggleButton rawBtn { "RAW" };
    StarButton favBtn;
    PixelSlider masterKnob { juce::Slider::RotaryVerticalDrag,
                             juce::Slider::NoTextBox };
    PixelScaleSlider scaleSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rawAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};

} // namespace blockwave::ui
