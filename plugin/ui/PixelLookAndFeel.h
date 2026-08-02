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

// BLOCKWAVE pixel LookAndFeel. All widget art is drawn from scratch:
//  - stepped pixel knob: 16-frame strip generated procedurally at first use
//    and cached in an Image (blitted with nearest-neighbour, never smoothed);
//  - chunky bevelled block buttons and toggles;
//  - bitmap-font button text and tooltips (PixelFont.h).
//
// Component IDs select special styles: "tab" (tab-strip buttons),
// "namebox" (sunken preset readout), "led" (labelled LED toggle).

#include <juce_gui_basics/juce_gui_basics.h>
#include "PixelTheme.h"
#include "PixelFont.h"

namespace blockwave::ui
{

class PixelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PixelLookAndFeel();

    static constexpr int kKnobSize   = 24;
    static constexpr int kKnobFrames = 16;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawTooltip (juce::Graphics&, const juce::String& text,
                      int width, int height) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

private:
    void buildKnobStrip();
    juce::Image knobStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelLookAndFeel)
};

} // namespace blockwave::ui
