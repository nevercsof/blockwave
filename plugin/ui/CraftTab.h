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
//   [ 3x3 bench ][ bench info    ][ material palette (7x2)            ]
//   [          ][ UNDO ][ REDO ] [                                    ]
//   [          ][ KEEP         ] [                                    ]
//   [ hint line ]               [ DICE ][ MUTATE ][ CLEAR ] / toast    ]
//   [ ................ 1.5-octave keyboard strip ...................  ]
//
// The bench INFO panel carries the controls that act on the bench's history
// and identity (UNDO / REDO / KEEP), and the action row under the palette
// carries the three that rewrite it (DICE / MUTATE / CLEAR). CLEAR moved out
// of the info panel to make room and to sit with its peers; nothing else on
// the tab moved, and the whole layout still fits the fixed 832x456 canvas.
//
// Threading: everything here is message thread. Grid edits go to the
// processor through setCraftGrid()/diceCraft()/mutateCraft(), which
// recompute the patch and write it through the atomic APVTS path — the UI
// never touches the audio thread. A single 15 Hz timer drives the discovery
// poll and every animation (well under the 30 Hz house limit; pixel
// animations are 2-4 discrete frames with no easing curves).
//
// Per-block MIX edits (the hover-revealed MIX label and its knob, see
// CraftBlocks.h) take the separate setCraftCellWeight() path instead: same
// atomic APVTS write, but no discovery registration and no name change,
// because weights are not part of a recipe's identity.

#include "../PluginProcessor.h"
#include "CraftBlocks.h"
#include "CraftHistory.h"
#include "KeyStrip.h"

namespace blockwave::ui
{

// Preset category a crafted sound is filed under, from its BASE archetype.
// Seven of the eight bases are also category names; DRONE has no category of
// its own and lands in FX, which is where the factory bank already files its
// DRONE-based atmospheres.
const char* presetCategoryForBase (blockwave::CraftBase) noexcept;

// Chunky block button with an original pixel glyph and a frame counter for
// its 4-frame press animation. A DISABLED button paints itself flat and
// greyed and takes neither clicks nor keyboard focus — which is how UNDO and
// REDO teach what they are for.
class PixelIconButton final : public juce::Button
{
public:
    enum class Glyph { none, dice, mutate, star, arrowLeft, arrowRight, broom,
                       undo, redo };

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
    void drawGlyph (juce::Graphics&, int x, int y, int scale, bool dim) const;

    juce::String text, subText;
    Glyph glyph;
    int textScale;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelIconButton)
};

// "★ RECIPE DISCOVERED: <NAME>" — 3-frame slide in, hold, 3-frame slide out.
// Also the CRAFT tab's general "that worked" plate: KEEP raises the same
// slab, in the same place, with its own headline and accent, because a
// preset that appears silently in a folder you are not looking at feels like
// nothing happened.
class DiscoveryToast final : public juce::Component
{
public:
    DiscoveryToast();

    void show (const juce::String& recipeName);
    void showSaved (const juce::String& presetName);
    void showProblem (const juce::String& headlineText, const juce::String& detail);
    void hideNow();
    bool animationTick();                       // true while visible
    void paint (juce::Graphics&) override;

    // What the plate is saying right now (component tests, tools).
    const juce::String& getHeadline() const noexcept { return headline; }
    const juce::String& getBody() const noexcept { return name; }

private:
    enum class Phase { hidden, entering, holding, leaving };

    void raise (const juce::String& headlineText, const juce::String& body,
                juce::Colour accentColour, bool withStar);

    juce::String headline { "RECIPE DISCOVERED" }, name;
    juce::Colour accent { colours::lava };
    bool starred = true;
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
    // componentIDs, so the tests can ask the BUTTON whether it is greyed out
    // rather than trusting a parallel accessor to agree with what is painted.
    static constexpr const char* kIdUndo = "craft_undo";
    static constexpr const char* kIdRedo = "craft_redo";
    static constexpr const char* kIdKeep = "craft_keep";

    explicit CraftTab (BlockwaveAudioProcessor&);
    ~CraftTab() override;

    // Pull grid / name / counter back from the processor (host state restore,
    // tab shown). History-neutral: the undo stack is left exactly as it was.
    void refreshFromProcessor();

    // A PRESET WAS LOADED. Same resync, plus the undo stack is thrown away —
    // new context, clean slate. Undo deliberately does not reach back across
    // a preset load (rationale in CraftHistory.h).
    void presetLoaded();

    // Hands the keyboard strip to the processor's lock-free MIDI inbox
    // (BlockwaveAudioProcessor::uiNoteOn/uiNoteOff). Until this is called the
    // strip paints itself as inert instead of lying about playing notes.
    void setMidiSink (std::function<void (int, float)> noteOn,
                      std::function<void (int)> noteOff);

    // Fired once per NEW recipe discovery, right when the toast appears; the
    // editor uses it to kick BlockwaveAudioProcessor::triggerDiscoveryJingle().
    std::function<void (const juce::String&)> onDiscovery;

    // Fired after KEEP has written a user preset and starred it, so the owner
    // can refresh the top bar (name / star) and the preset browser.
    std::function<void()> onPresetSaved;

    void showDiscoveries (bool shouldShow);
    bool isShowingDiscoveries() const;

    // ---- undo / redo (producer request) ------------------------------------
    // Bench edits only; see CraftHistory.h for the scope decision. Every
    // restore goes back out through setCraftGrid() — the processor's single
    // write path — so the APVTS-authoritative cell weights and the grid
    // mirror can never drift apart.
    bool canUndo() const noexcept { return history.canUndo(); }
    bool canRedo() const noexcept { return history.canRedo(); }
    int getUndoDepth() const noexcept { return history.getNumUndoSteps(); }
    int getRedoDepth() const noexcept { return history.getNumRedoSteps(); }
    bool undo();                                 // false when there is nothing
    bool redo();

    // ---- KEEP: save the bench as a user preset AND star it ------------------
    // One click. Names it after the active recipe, else the auto-generated
    // patch name, uniquified against the whole library; files it under the
    // category its BASE maps to; stars it; raises the toast. Returns false
    // when there is no bench to keep or the write failed.
    bool keepCurrentSound();
    const juce::String& getLastKeptName() const noexcept { return lastKeptName; }
    const juce::String& getLastKeptCategory() const noexcept { return lastKeptCategory; }

    // Tool hooks (tools/screenshots, component tests): each drives the SAME
    // code path the mouse and keyboard drive, so a rendered MIX state is the
    // real one rather than a mock-up. nudge steps are 5 % each.
    void nudgeCellWeight (int slot, int steps) { gridComp.nudgeCellWeight (slot, steps); }
    void setMixKnobOpen (int slot, bool open)  { gridComp.setMixKnobOpen (slot, open); }
    // Read-back for the same hooks: what the BENCH shows, which is not always
    // what the processor holds (that gap was defect 5 — a preset carrying no
    // craft used to leave the previous blocks and their open knobs on screen).
    bool isMixKnobOpen (int slot) const        { return gridComp.isMixKnobOpen (slot); }
    const CraftGrid& getShownGrid() const noexcept { return gridComp.getGrid(); }
    bool isShowingCraftGrid() const noexcept   { return hasGrid; }
    void setCellHoverForDisplay (int slot, bool overCell, bool overLabel)
    {
        gridComp.setHoverForDisplay (slot, overCell, overLabel);
    }
    // Same idea for the CRAFTING gestures themselves (tools/presskit renders a
    // frame sequence of a real craft). Each forwards to the exact call the
    // mouse handler makes, so the placement flash, the palette arming, the
    // grid edit -> processor -> recipe match -> toast chain are all the
    // production ones; nothing here is a mock.
    void clickMaterialForDisplay (Material m)          { tileClicked (m); }
    void clickCellForDisplay (int slot, bool rightBtn) { gridComp.cellClicked (slot, rightBtn); }
    void cycleBaseForDisplay (int delta)               { gridComp.cycleBase (delta); }
    void clearBenchForDisplay()                        { doClear(); }
    void diceForDisplay()                              { doDice(); }
    void mutateForDisplay()                            { doMutate(); }
    // A whole MIX-KNOB DRAG, framed exactly as the mouse frames it: one host
    // gesture, one undo step, however many values pass through the middle.
    void dragCellWeightForDisplay (int slot, const float* weights01, int count);
    // Steps the 15 Hz timer by hand. The toast and every button animation are
    // 2-4 discrete frames driven from there, so this is how an offscreen
    // renderer gets a settled frame without waiting on real time.
    void tickForDisplay (int frames);
    const DiscoveryToast& getToast() const noexcept { return toast; }
    bool isToastVisible() const noexcept { return toast.isVisible(); }
    juce::Rectangle<int> getUndoRedoBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;

    void gridEdited (const CraftGrid&);
    void tileClicked (Material);
    void doDice();
    void doMutate();
    void doClear();
    void refreshLabels();
    juce::String hintText() const;

    // Undo plumbing. commitEdit() snapshots the patch AFTER an edit lands;
    // restore() puts one back and re-reads everything from the processor.
    void commitEdit (CraftHistory::Edit kind, int slot = -1);
    void restore (const CraftState&);
    void refreshHistoryButtons();

    BlockwaveAudioProcessor& proc;
    BlockImageCache imageCache;

    CraftGridComponent gridComp { imageCache };
    MaterialPalette palette { imageCache };
    KeyStrip keys;
    DiscoveryToast toast;
    DiscoveriesPanel discoveriesPanel;

    PixelIconButton diceBtn { "DICE", PixelIconButton::Glyph::dice };
    PixelIconButton mutateBtn { "MUTATE", PixelIconButton::Glyph::mutate };
    PixelIconButton clearBtn { "CLEAR", PixelIconButton::Glyph::broom };
    PixelIconButton basePrev { "", PixelIconButton::Glyph::arrowLeft, 1 };
    PixelIconButton baseNext { "", PixelIconButton::Glyph::arrowRight, 1 };
    PixelIconButton discoveriesBtn { "", PixelIconButton::Glyph::star, 1 };
    PixelIconButton undoBtn { "UNDO", PixelIconButton::Glyph::undo, 1 };
    PixelIconButton redoBtn { "REDO", PixelIconButton::Glyph::redo, 1 };
    PixelIconButton keepBtn { "KEEP", PixelIconButton::Glyph::star, 1 };

    // Pure UI/session state: never serialised, gone when the editor closes.
    CraftHistory history;
    // Did the knob drag currently in flight actually change anything? A press
    // and release that never moved is not an edit and gets no undo step.
    bool weightDragMoved = false;
    juce::String lastKeptName, lastKeptCategory;

    juce::String patchName, recipeName;
    bool hasGrid = false;
    int nameGlitch = 0;                          // MUTATE name-plate frames
    int syncCountdown = 15;                      // external-change safety net
    juce::Random glitchRng { 0x6d17 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CraftTab)
};

} // namespace blockwave::ui
