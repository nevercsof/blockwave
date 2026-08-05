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

// The CRAFT tab — BLOCKWAVE's home screen (docs/CRAFT_GRID.md, SPEC §UI).
//
//   [ auto-generated patch name .................. ][ n/16 discoveries ]
//   [ 3x3 bench ][ bench info ][ material palette (7x2)               ]
//   [ hint line ][            ][ DICE ][ MUTATE ][ discovery toast    ]
//   [ ................ 1.5-octave keyboard strip ...................  ]
//
// Threading: everything here is message thread. Grid edits go to the
// processor through setCraftGrid()/diceCraft()/mutateCraft(), which
// recompute the patch and write it through the atomic APVTS path — the UI
// never touches the audio thread. A single 15 Hz timer drives the discovery
// poll and every animation (well under the 30 Hz house limit; pixel
// animations are 2-4 discrete frames with no easing curves).
//
// Per-block MIX edits (the rail on each filled block, see CraftBlocks.h) take
// the separate setCraftCellWeight() path instead: same atomic APVTS write,
// but no discovery registration and no name change, because weights are not
// part of a recipe's identity.

#include "../PluginProcessor.h"
#include "CraftBlocks.h"
#include "KeyStrip.h"

namespace blockwave::ui
{

// Chunky block button with an original pixel glyph and a frame counter for
// its 4-frame press animation.
class PixelIconButton final : public juce::Button
{
public:
    enum class Glyph { none, dice, mutate, star, arrowLeft, arrowRight, broom };

    PixelIconButton (const juce::String& text, Glyph glyph, int textScale = 2);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    // Own text state: the button paints bitmap-font glyphs, not LookAndFeel
    // text, so it keeps its own label (and an optional small caption above).
    void setText (const juce::String&);
    void setSubText (const juce::String&);

    void startAnimation (int frames);
    bool animationTick();                       // true while animating
    int getFrame() const noexcept { return frame; }

private:
    void drawGlyph (juce::Graphics&, int x, int y, int scale) const;

    juce::String text, subText;
    Glyph glyph;
    int textScale;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelIconButton)
};

// "★ RECIPE DISCOVERED: <NAME>" — 3-frame slide in, hold, 3-frame slide out.
class DiscoveryToast final : public juce::Component
{
public:
    DiscoveryToast();

    void show (const juce::String& recipeName);
    void hideNow();
    bool animationTick();                       // true while visible
    void paint (juce::Graphics&) override;

private:
    enum class Phase { hidden, entering, holding, leaving };

    juce::String name;
    Phase phase = Phase::hidden;
    int frame = 0, holdTicks = 0, sparkle = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiscoveryToast)
};

// Persistent Discoveries page: every recipe slot, found ones by name PLUS
// their 3x3 pattern as a 2x mini icon (a shareable, screenshot-crisp cheat
// card), the rest locked as ???? with no pattern. No hints beyond the
// counter (CRAFT_GRID.md).
class DiscoveriesPanel final : public juce::Component
{
public:
    DiscoveriesPanel (const RecipeBook& book, DiscoveryStore& store);

    std::function<void()> onClose;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    juce::Rectangle<int> panelRect() const;

    const RecipeBook& recipes;
    DiscoveryStore& discoveries;
    juce::TextButton closeBtn { "X" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiscoveriesPanel)
};

// ---------------------------------------------------------------------------

class CraftTab final : public juce::Component,
                       public juce::DragAndDropContainer,
                       private juce::Timer
{
public:
    explicit CraftTab (BlockwaveAudioProcessor&);
    ~CraftTab() override;

    // Pull grid / name / counter back from the processor (preset load, host
    // state restore, tab shown).
    void refreshFromProcessor();

    // Hands the keyboard strip to the processor's lock-free MIDI inbox
    // (BlockwaveAudioProcessor::uiNoteOn/uiNoteOff). Until this is called the
    // strip paints itself as inert instead of lying about playing notes.
    void setMidiSink (std::function<void (int, float)> noteOn,
                      std::function<void (int)> noteOff);

    // Fired once per NEW recipe discovery, right when the toast appears; the
    // editor uses it to kick BlockwaveAudioProcessor::triggerDiscoveryJingle().
    std::function<void (const juce::String&)> onDiscovery;

    void showDiscoveries (bool shouldShow);
    bool isShowingDiscoveries() const;

    // Tool hook (tools/screenshots): drives the SAME code path as the arrow
    // keys on a focused block, so a rendered mix state is the real one rather
    // than a mock-up. steps are 5 % each.
    void nudgeCellWeight (int slot, int steps) { gridComp.nudgeCellWeight (slot, steps); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    void gridEdited (const CraftGrid&);
    void tileClicked (Material);
    void doDice();
    void doMutate();
    void doClear();
    void refreshLabels();
    juce::String hintText() const;

    BlockwaveAudioProcessor& proc;
    BlockImageCache imageCache;

    CraftGridComponent gridComp { imageCache };
    MaterialPalette palette { imageCache };
    KeyStrip keys;
    DiscoveryToast toast;
    DiscoveriesPanel discoveriesPanel;

    PixelIconButton diceBtn { "DICE", PixelIconButton::Glyph::dice };
    PixelIconButton mutateBtn { "MUTATE", PixelIconButton::Glyph::mutate };
    PixelIconButton clearBtn { "CLEAR", PixelIconButton::Glyph::broom, 1 };
    PixelIconButton basePrev { "", PixelIconButton::Glyph::arrowLeft, 1 };
    PixelIconButton baseNext { "", PixelIconButton::Glyph::arrowRight, 1 };
    PixelIconButton discoveriesBtn { "", PixelIconButton::Glyph::star, 1 };

    juce::String patchName, recipeName;
    bool hasGrid = false;
    int nameGlitch = 0;                          // MUTATE name-plate frames
    int syncCountdown = 15;                      // external-change safety net
    juce::Random glitchRng { 0x6d17 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CraftTab)
};

} // namespace blockwave::ui
