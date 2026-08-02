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

#include "PixelLookAndFeel.h"
#include <cmath>

namespace blockwave::ui
{

PixelLookAndFeel::PixelLookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, colours::chip);
}

// ---- stepped pixel knob -----------------------------------------------------

void PixelLookAndFeel::buildKnobStrip()
{
    using namespace colours;
    knobStrip = juce::Image (juce::Image::ARGB,
                             kKnobFrames * kKnobSize, kKnobSize, true);

    const juce::Colour ringLight { 0xffb2b2ba };
    const juce::Colour ringDark  { 0xff565660 };
    const juce::Colour face      { 0xff7e7e88 };

    for (int f = 0; f < kKnobFrames; ++f)
    {
        const float pos = static_cast<float> (f) / static_cast<float> (kKnobFrames - 1);
        const float ang = juce::MathConstants<float>::pi * (-0.75f + 1.5f * pos);
        const int   ox  = f * kKnobSize;

        for (int y = 0; y < kKnobSize; ++y)
        {
            for (int x = 0; x < kKnobSize; ++x)
            {
                const float dx = static_cast<float> (x) - 11.5f;
                const float dy = static_cast<float> (y) - 11.5f;
                const float r  = std::sqrt (dx * dx + dy * dy);
                if (r > 10.5f)
                    continue;
                juce::Colour c;
                if (r > 9.4f)
                    c = outline;
                else if (r > 6.5f)
                    c = (dx + dy < -2.0f) ? ringLight
                      : (dx + dy >  2.0f) ? ringDark : stone;
                else
                    c = face;
                knobStrip.setPixelAt (ox + x, y, c);
            }
        }

        // Pointer: 1-px stepped line from centre, ice blue, drawn last.
        const float sx = std::sin (ang);
        const float sy = -std::cos (ang);
        for (float rr = 2.0f; rr <= 8.6f; rr += 0.5f)
        {
            const int px = static_cast<int> (std::lround (11.5f + sx * rr));
            const int py = static_cast<int> (std::lround (11.5f + sy * rr));
            knobStrip.setPixelAt (ox + px, py, ice);
        }
    }
}

void PixelLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y,
                                         int width, int height,
                                         float sliderPosProportional,
                                         float, float, juce::Slider& slider)
{
    if (! knobStrip.isValid())
        buildKnobStrip();

    const int frame = juce::jlimit (0, kKnobFrames - 1,
        static_cast<int> (std::lround (sliderPosProportional
                                       * static_cast<float> (kKnobFrames - 1))));
    const int dx = x + (width  - kKnobSize) / 2;
    const int dy = y + (height - kKnobSize) / 2;

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (knobStrip, dx, dy, kKnobSize, kKnobSize,
                 frame * kKnobSize, 0, kKnobSize, kKnobSize);

    if (slider.hasKeyboardFocus (true))
        drawFocusTicks (g, { dx - 2, dy - 2, kKnobSize + 4, kKnobSize + 4 });
}

// ---- buttons ---------------------------------------------------------------

void PixelLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                             const juce::Colour&,
                                             bool highlighted, bool down)
{
    using namespace colours;
    const auto r  = b.getLocalBounds();
    const auto id = b.getComponentID();

    if (id == "tab")
    {
        const bool on = b.getToggleState();
        drawBevelBox (g, r, on ? panelFace : titleBar,
                      on ? panelLight : panelDark,
                      on ? panelDark : outline, outline, ! on);
        if (on)                                        // lit grass edge on top
        {
            g.setColour (grass);
            g.fillRect (r.getX() + 2, r.getY() + 1, r.getWidth() - 4, 2);
        }
    }
    else if (id == "namebox")
    {
        drawBevelBox (g, r, chip, panelLight, panelDark, outline, true);
    }
    else
    {
        auto face = buttonFace;
        if (down)             face = juce::Colour (0xff6a6a72);
        else if (highlighted) face = juce::Colour (0xff9c9ca4);
        drawBevelBox (g, r, face, panelLight, panelDark, outline, down);
    }

    if (b.hasKeyboardFocus (true))
        drawFocusTicks (g, r);
}

void PixelLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                       bool, bool down)
{
    using namespace colours;
    const auto id = b.getComponentID();
    juce::Colour c = buttonText;
    if (id == "tab")
        c = b.getToggleState() ? label : dimText;
    else if (id == "namebox")
        c = ice;

    auto area = b.getLocalBounds();
    if (down)
        area.translate (0, 1);
    drawPixelTextCentred (g, b.getButtonText(), area, 1, c);
}

void PixelLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                         bool highlighted, bool)
{
    using namespace colours;
    const auto r  = b.getLocalBounds();
    const bool on = b.getToggleState();

    if (b.getComponentID() == "led")
    {
        // Label text left, square LED block right (lava when lit).
        const int ledSize = 10;
        const juce::Rectangle<int> led (r.getRight() - ledSize,
                                        r.getCentreY() - ledSize / 2,
                                        ledSize, ledSize);
        drawPixelText (g, b.getButtonText(), r.getX(),
                       r.getCentreY() - 2, 1, on ? lava : dimText);
        drawBevelBox (g, led, on ? lava : ledOff,
                      on ? juce::Colour (0xffffb066) : panelDark,
                      on ? juce::Colour (0xffb35012) : panelLight,
                      outline, false, 2);
    }
    else
    {
        auto face = on ? grass : juce::Colour (0xff54545e);
        if (highlighted)
            face = face.brighter (0.1f);
        drawBevelBox (g, r, face, panelLight, panelDark, outline, on);
        if (on)                                        // pixel glint, no glow
        {
            g.setColour (juce::Colour (0xff9fdf7a));
            g.fillRect (r.getX() + 3, r.getY() + 3, 3, 2);
        }
    }

    if (b.hasKeyboardFocus (true))
        drawFocusTicks (g, r);
}

// ---- tooltip (3-word product voice) ----------------------------------------

void PixelLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text,
                                    int width, int height)
{
    using namespace colours;
    drawBevelBox (g, { 0, 0, width, height }, chip, panelDark, outline, outline);
    drawPixelTextCentred (g, text, { 0, 0, width, height }, 1, ice);
}

juce::Rectangle<int> PixelLookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                         juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    const int w = pixelTextWidth (tipText, 1) + 12;
    const int h = 16;
    return juce::Rectangle<int> (screenPos.x + 8, screenPos.y + 16, w, h)
               .constrainedWithin (parentArea);
}

} // namespace blockwave::ui
