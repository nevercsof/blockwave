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

// The crafting bench itself (docs/CRAFT_GRID.md §Implementation notes):
//
//   CraftGridComponent — 3x3 of 64px cells: centre = BASE archetype
//     (click / RETURN cycles it), 8 outer cells = MATERIALS. Every cell is a
//     drag-and-drop target AND a drag source (moving a block between cells
//     matters: recipes are position-sensitive). Right-click clears a cell.
//     Keyboard path: cells are focusable, arrows walk the grid, RETURN
//     places the armed material / selects the cell, DELETE clears.
//
// ---- HIDDEN PER-BLOCK MIX (producer spec; docs/CRAFT_GRID.md §WEIGHT) ------
// The mix control is INVISIBLE until you go looking for it. Specified by the
// producer, verbatim, and implemented literally:
//
//   at rest      nothing at all on the block — just the block art;
//   on hover     a 3x5 bitmap "MIX" appears in the block's TOP-RIGHT corner:
//                bare letters in a muted tone, no outline, no bevel, no
//                backing plate;
//   label hover  the letters light up to full contrast;
//   label click  the mix KNOB opens on the block; clicking again hides it.
//
// The expanded state is pure UI state, per cell: it is never written into a
// preset or the session, and it resets when the cell is cleared (or the bench
// is replaced by a preset load).
//
// HIT-TESTING — the knob must never eat the block's drag gesture. Positions
// are load-bearing on a 3x3 bench (recipes are position-sensitive), so
// "drag this block one row up" has to keep working while the knob is open:
//
//   MIX label   16x12 at the top-right corner. Owns the CLICK only: the press
//               is armed on mouseDown and toggles on mouseUp, so as soon as
//               the pointer travels 4 px the press becomes a normal block
//               drag-and-drop instead. You can drag a block by its label.
//   MIX knob    28x28 at the bottom-right corner, and ONLY while open. Owns
//               press-and-drag (vertical drag sets the weight).
//   everything  ~80 % of the cell: unchanged — click selects, drag moves the
//   else        block, right-click clears, an ARMED material places.
//
// Right-click and an armed material are checked FIRST and apply to every
// pixel of the cell, label and knob included: the guided
// click-cell-then-click-material flow is never dead anywhere.
//
// Keyboard: M toggles the knob on the focused cell (the label is drawn while
// a cell has keyboard focus, so the shortcut advertises itself). UP/DOWN
// adjust the mix while that cell's knob is open (SHIFT = 25 % jumps) and
// otherwise walk the bench by rows as usual; LEFT/RIGHT always walk the whole
// grid in reading order with wrap. The wheel nudges 5 % only over an OPEN
// knob's cell — with no control on screen there are no invisible edits.
//
// What stays: the block art DARKENS in 5 discrete steps as the weight drops
// and anything below 100 % keeps its small nn% tag, so a turned-down bench is
// still readable at a glance without touching anything. Weights never affect
// recipe detection or the auto-name (engine guarantee) — nothing in this UI
// implies otherwise.
//
// (This replaces the earlier always-visible mix rail down the block's right
// edge. The rail worked, but it was permanent chrome on every filled block
// and it owned 12 px of the drag zone; the producer asked for the control to
// be hidden until hovered.)
//
//   MaterialPalette — 7x2 tiles of the 14 materials. Drag a tile onto a
//     cell, or click a tile to ARM it and then click a cell (the accessible
//     fallback, also the fastest path on a trackpad). Hover shows the
//     3-word character tooltip.
//
// Both are pure UI: they own a CraftGrid copy and report edits through
// onGridEdited. Pushing the grid at the processor (and therefore all
// parameter traffic) happens one level up, in CraftTab, on the message
// thread.

#include "MaterialArt.h"
#include "PixelFont.h"

namespace blockwave::ui
{

constexpr int kCraftCell    = 64;                 // block size on the bench
constexpr int kCraftGap     = 8;
constexpr int kCraftGridW   = kCraftCell * 3 + kCraftGap * 2;   // 208
constexpr int kPaletteTileW = 48;
constexpr int kPaletteTileH = 48;

// Hidden-MIX geometry (sub-cell detail: 4-px grid, per PixelTheme's rule).
constexpr int kMixLabelW     = 16;                // "MIX" hit box, top-right
constexpr int kMixLabelH     = 12;
constexpr int kMixKnobArt    = 24;                // knob sprite, 16 frames
constexpr int kMixKnobFrames = 16;
constexpr int kMixKnobBox    = 28;                // knob hit box, bottom-right
constexpr int kMixDragTravel = 48;                // px of drag = full 0..100 %
constexpr float kMixStep     = 0.05f;             // knob/keyboard snap: 5 %
constexpr int kMixReadoutTicks = 8;               // ~0.5 s at the 15 Hz timer

// Drag payloads: "M:<material index>" from the palette, "C:<cell>" from a
// bench cell. Parsed by CraftGridComponent's cells.
juce::String makeMaterialDragId (Material m);
juce::String makeCellDragId (int cellIndex);

class CraftGridComponent;

// One 64x64 bench cell (outer material slot or the centre base slot).
class CraftCell final : public juce::Component,
                        public juce::SettableTooltipClient,
                        public juce::DragAndDropTarget
{
public:
    CraftCell (CraftGridComponent& owner, int slotIndex, bool isBaseSlot);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    int getSlot() const noexcept { return slot; }
    bool isBase() const noexcept { return baseSlot; }

    // Display-only seam for tools/screenshots and the component tests: sets
    // exactly the two flags the real mouseEnter/mouseMove handlers set, so a
    // rendered hover state is the genuine one and not a mock-up.
    void setHoverForDisplay (bool overCell, bool overLabel);

private:
    juce::Rectangle<int> mixLabelBounds() const;   // "MIX" hit box, top-right
    juce::Rectangle<int> mixKnobBounds() const;    // knob hit box, bottom-right
    bool hasMixControl() const;                    // filled outer cell?
    bool isMixOpen() const;
    void paintMixLabel (juce::Graphics&, Material, float weight01);
    void paintMixKnob (juce::Graphics&, float weight01);
    void paintMixTag (juce::Graphics&, float weight01);
    void paintMixReadout (juce::Graphics&, float weight01);

    CraftGridComponent& grid;
    const int slot;                                // 0..7, or -1 for the base
    const bool baseSlot;
    bool hovering = false, dragOver = false, labelHot = false;
    bool labelArmed = false;                       // press landed on the label
    bool mixDragging = false;
    int mixDragStartY = 0;
    float mixDragStartWeight = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CraftCell)
};

class CraftGridComponent final : public juce::Component
{
public:
    explicit CraftGridComponent (BlockImageCache& cache);

    // Reported on every user edit (drop, click-place, clear, base cycle).
    std::function<void (const CraftGrid&)> onGridEdited;
    // Fired when the selected cell changes (CraftTab disarms the palette).
    std::function<void (int)> onSelectionChanged;
    // Fired on every knob / keyboard / wheel weight change (slot, 0..1). Kept
    // SEPARATE from onGridEdited on purpose: a weight edit must go through
    // the processor's setCraftCellWeight() path, which re-crafts WITHOUT
    // registering a discovery and without touching the recipe/auto name.
    std::function<void (int, float)> onCellWeightEdited;
    // Knob-drag FRAMING, fired on the mouse-down and mouse-up of a MIX knob
    // drag and on nothing else. Two jobs, both of which need the whole drag
    // rather than its individual steps:
    //   - the owner wraps the run of onCellWeightEdited calls in ONE host
    //     parameter gesture (begin/endCraftCellWeightGesture), which is what
    //     a host records cleanly and what makes automation-write behave;
    //   - the undo stack records ONE step for the drag instead of one per
    //     pixel of travel.
    // Fired even when the drag never moved, so a gesture can never be left
    // open; the owner is expected to notice that nothing changed.
    std::function<void (int)> onCellWeightGestureStart, onCellWeightGestureEnd;

    const CraftGrid& getGrid() const noexcept { return grid; }
    void setGrid (const CraftGrid&);              // external sync, no callback

    // Materials shown but not owned by the grid: "" when nothing is armed.
    void setArmedMaterial (Material m) noexcept { armed = m; }
    Material getArmedMaterial() const noexcept { return armed; }

    int getSelectedSlot() const noexcept { return selected; }
    void selectSlot (int slot);

    // Cell edits (each one notifies onGridEdited).
    void placeMaterial (int slot, Material m);
    void clearSlot (int slot);
    void moveMaterial (int fromSlot, int toSlot);
    void cycleBase (int delta);

    // Cell-click behaviour: place the armed material, else select the cell.
    void cellClicked (int slot, bool isRightClick);
    void focusNeighbour (int slot, int dx, int dy);
    void focusStep (int slot, int delta);          // linear walk, wraps

    // ---- per-cell WEIGHT / MIX --------------------------------------------
    float cellWeight (int slot) const;
    // Knob drag: beginMixDrag() on mouse-down, any number of
    // setCellWeightFromDrag() (snaps to kMixStep and HOLDS the big readout
    // up), endMixDrag() on mouse-up. No-op on an empty cell.
    void beginMixDrag (int slot);
    void setCellWeightFromDrag (int slot, float weight01);
    void endMixDrag();
    bool isMixDragging() const noexcept { return mixDragSlot >= 0; }
    // Keyboard / wheel: +-steps * kMixStep, readout shown for a moment.
    void nudgeCellWeight (int slot, int steps);
    // Which cell shows the big % readout right now (-1 = none).
    int mixReadoutSlot() const noexcept { return readoutSlot; }

    // Is this cell's mix knob expanded? PURE UI STATE: never serialised into
    // a preset or the session, cleared when the cell is emptied, when a new
    // block is placed on it, and whenever the whole bench is replaced.
    bool isMixKnobOpen (int slot) const noexcept;
    void setMixKnobOpen (int slot, bool shouldBeOpen);
    void toggleMixKnob (int slot);            // the MIX label click + the M key

    // Display-only seam (tools/screenshots, component tests).
    void setHoverForDisplay (int slot, bool overCell, bool overLabel);

    // 16-frame stepped knob strip, rendered once and blitted per cell (house
    // rule: static art is cached into Images, never redrawn per repaint).
    const juce::Image& mixKnobStrip();

    // DICE: 3 scramble frames, then the real result — 4 frames total, no
    // easing curves (pixel art rules). Driven by CraftTab's 15 Hz timer.
    void startDiceAnimation();
    void flashSlot (int slot);
    bool animationTick();                          // true while animating
    bool isAnimating() const noexcept { return diceFrames > 0; }

    // What a cell should actually draw right now (scramble-aware).
    Material displayedMaterial (int slot) const;
    int slotFlash (int slot) const;
    BlockImageCache& getCache() const noexcept { return cache; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BlockImageCache& cache;
    CraftGrid grid;
    Material armed = Material::none;
    int selected = -1;
    int diceFrames = 0;
    int flash[kNumCells] = {};
    Material scramble[kNumCells] = {};
    juce::Random rng;
    std::vector<std::unique_ptr<CraftCell>> cells;   // 8 outer + 1 base (last)

    int readoutSlot = -1;                           // big % badge owner
    int readoutTicks = 0;                           // 0 while a drag holds it
    bool readoutHeld = false;
    int mixDragSlot = -1;                           // cell owning the live drag
    bool mixOpen[kNumCells] = {};                   // expanded knobs (UI only)
    juce::Image knobStrip;                          // built on first use

    void notifyEdit();
    void applyCellWeight (int slot, float weight01, bool hold);
    void refreshCellTooltips();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CraftGridComponent)
};

// ---------------------------------------------------------------------------

class MaterialPalette;

class MaterialTile final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    MaterialTile (MaterialPalette& owner, Material m);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    Material getMaterial() const noexcept { return material; }

private:
    MaterialPalette& palette;
    const Material material;
    bool hovering = false, dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialTile)
};

class MaterialPalette final : public juce::Component
{
public:
    explicit MaterialPalette (BlockImageCache& cache);

    static constexpr int kCols = 7, kRows = 2;
    static constexpr int width  = 12 * 2 + kCols * kPaletteTileW + (kCols - 1) * 8;
    static constexpr int height = 24 + kRows * kPaletteTileH + (kRows - 1) * 8 + 8;

    // A tile was clicked (not dragged): CraftTab arms it or drops it into the
    // selected cell.
    std::function<void (Material)> onTileClicked;

    void setArmed (Material m);
    Material getArmed() const noexcept { return armed; }

    void paint (juce::Graphics&) override;
    void resized() override;

    BlockImageCache& getCache() const noexcept { return cache; }
    bool isArmed (Material m) const noexcept { return armed == m; }

private:
    BlockImageCache& cache;
    Material armed = Material::none;
    std::vector<std::unique_ptr<MaterialTile>> tiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaterialPalette)
};

} // namespace blockwave::ui
