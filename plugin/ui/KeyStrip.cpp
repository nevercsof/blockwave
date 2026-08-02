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

#include "KeyStrip.h"

namespace blockwave::ui
{

KeyStrip::KeyStrip()
{
    setWantsKeyboardFocus (true);
    setTooltip ("audition the patch");
    setSize (width, height);
}

KeyStrip::~KeyStrip()
{
    stopNote();
}

bool KeyStrip::isBlackKey (int semitone) noexcept
{
    const int s = semitone % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}

int KeyStrip::whiteIndexFor (int semitone) noexcept
{
    // Number of white keys strictly below this semitone.
    static const int whitesBelow[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
    return (semitone / 12) * 7 + whitesBelow[semitone % 12];
}

juce::Rectangle<int> KeyStrip::keyBounds (int semitone) const
{
    if (! isBlackKey (semitone))
        return { whiteIndexFor (semitone) * kWhiteW, 0, kWhiteW, kWhiteH };

    // Black keys straddle the boundary above the white key below them
    // (whiteIndexFor counts the whites strictly below this semitone).
    const int boundary = whiteIndexFor (semitone) * kWhiteW;
    return { boundary - kBlackW / 2, 0, kBlackW, kBlackH };
}

int KeyStrip::keyAt (juce::Point<int> pos) const
{
    for (int s = 0; s < kNumKeys; ++s)               // black keys win
        if (isBlackKey (s) && keyBounds (s).contains (pos))
            return s;
    for (int s = 0; s < kNumKeys; ++s)
        if (! isBlackKey (s) && keyBounds (s).contains (pos))
            return s;
    return -1;
}

void KeyStrip::setLive (bool isLive)
{
    live = isLive;
    repaint();
}

void KeyStrip::paint (juce::Graphics& g)
{
    using namespace colours;

    for (int s = 0; s < kNumKeys; ++s)
    {
        if (isBlackKey (s))
            continue;
        const auto r = keyBounds (s);
        const bool down = held == s;
        drawBevelBox (g, r, down ? dirt : juce::Colour (0xffd8d8e0),
                      down ? juce::Colour (0xffa4703f) : juce::Colours::white,
                      juce::Colour (0xff86868e), outline, down);
        if (s % 12 == 0)                             // octave marker: C3 / C4
            drawPixelText (g, "C" + juce::String (3 + s / 12),
                           r.getX() + 6, r.getBottom() - 12, 1,
                           down ? label : juce::Colour (0xff6e6e76));
        if (focusKey == s && hasKeyboardFocus (false))
            drawFocusTicks (g, r.reduced (2));
    }

    for (int s = 0; s < kNumKeys; ++s)
    {
        if (! isBlackKey (s))
            continue;
        const auto r = keyBounds (s);
        const bool down = held == s;
        drawBevelBox (g, r, down ? lava : juce::Colour (0xff26262e),
                      down ? juce::Colour (0xffffb066) : juce::Colour (0xff44444e),
                      outline, outline, down);
        if (focusKey == s && hasKeyboardFocus (false))
            drawFocusTicks (g, r.reduced (2));
    }

    if (! live)
    {
        // Honest state: the strip is drawn but cannot reach the synth yet.
        const int w = pixelTextWidth ("NO MIDI PATH", 1) + 8;
        const juce::Rectangle<int> tag (getWidth() - w - 8, 4, w, 12);
        g.setColour (night);
        g.fillRect (tag);
        drawPixelTextCentred (g, "NO MIDI PATH", tag, 1, lava);
    }
}

void KeyStrip::startNote (int semitone)
{
    if (semitone < 0 || semitone >= kNumKeys || semitone == held)
        return;
    stopNote();
    held = semitone;
    if (live && onNoteOn)
        onNoteOn (kFirstNote + semitone, 0.8f);
    repaint();
}

void KeyStrip::stopNote()
{
    if (held < 0)
        return;
    const int note = kFirstNote + held;
    held = -1;
    if (live && onNoteOff)
        onNoteOff (note);
    repaint();
}

void KeyStrip::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int s = keyAt (e.getPosition());
    if (s >= 0)
    {
        focusKey = s;
        startNote (s);
    }
}

void KeyStrip::mouseDrag (const juce::MouseEvent& e)
{
    const int s = keyAt (e.getPosition());
    if (s >= 0 && s != held)
        startNote (s);
    else if (s < 0)
        stopNote();
}

void KeyStrip::mouseUp (const juce::MouseEvent&) { stopNote(); }
void KeyStrip::mouseExit (const juce::MouseEvent&) { stopNote(); }

bool KeyStrip::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::leftKey))
    {
        focusKey = juce::jmax (0, focusKey - 1);
        repaint();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::rightKey))
    {
        focusKey = juce::jmin (kNumKeys - 1, focusKey + 1);
        repaint();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::returnKey) || k.isKeyCode (juce::KeyPress::spaceKey))
    {
        // No key-up event from JUCE here, so the keyboard path plays a short
        // fixed note and releases it from the message thread.
        startNote (focusKey);
        juce::Component::SafePointer<KeyStrip> safe (this);
        juce::Timer::callAfterDelay (250, [safe]
        {
            if (safe != nullptr)
                safe->stopNote();
        });
        return true;
    }
    return false;
}

} // namespace blockwave::ui
