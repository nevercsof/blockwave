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

// 1.5-octave clickable pixel keyboard (SPEC §UI, CRAFT tab): C3..F4, chunky
// bevelled keys, no anti-aliasing anywhere. Click/drag to audition, arrow
// keys walk the keys and RETURN plays the focused one.
//
// The strip is purely a UI surface: it emits onNoteOn / onNoteOff on the
// message thread and never touches the audio thread itself. The editor hands
// those events to the processor's lock-free inbox (src/UiMidiQueue.h) via
// CraftTab::setMidiSink; the strip goes live as soon as that happens.

#include "PixelTheme.h"
#include "PixelFont.h"

namespace blockwave::ui
{

class KeyStrip final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    static constexpr int kFirstNote = 48;          // C3
    static constexpr int kNumKeys   = 18;          // 1.5 octaves, C3..F4
    static constexpr int kWhiteW = 64, kWhiteH = 64;
    static constexpr int kBlackW = 40, kBlackH = 40;
    static constexpr int kNumWhite = 11;
    static constexpr int width  = kNumWhite * kWhiteW;
    static constexpr int height = kWhiteH;

    KeyStrip();
    ~KeyStrip() override;

    std::function<void (int midiNote, float velocity)> onNoteOn;
    std::function<void (int midiNote)> onNoteOff;

    // False when the editor has no way to reach the synth (setMidiSink never
    // called): the strip then paints a small "no midi path" marker instead of
    // pretending to play. The plugin editor always wires the sink, so the tag
    // only shows in harnesses that build a bare CraftTab.
    void setLive (bool isLive);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    static bool isBlackKey (int semitone) noexcept;
    static int whiteIndexFor (int semitone) noexcept;
    juce::Rectangle<int> keyBounds (int semitone) const;
    int keyAt (juce::Point<int> pos) const;        // -1 = none
    void startNote (int semitone);
    void stopNote();

    int held = -1;                                 // semitone currently down
    int focusKey = 0;
    bool live = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyStrip)
};

} // namespace blockwave::ui
