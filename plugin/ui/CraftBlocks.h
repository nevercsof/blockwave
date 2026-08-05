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
// ---- PER-BLOCK MIX RAIL (producer request; docs/CRAFT_GRID.md §WEIGHT) -----
// Every FILLED cell carries a vertical mix slider ON the block itself: a
// 12-px rail down its right edge with a chunky handle and a filled track.
// Dragging the handle up/down sets that cell's weight (0..100 %, snapped to
// 5 %) and the block art DARKENS in 5 discrete steps as the weight drops.
//
// WHY A RAIL AND NOT A BARE VERTICAL DRAG ON THE BLOCK FACE:
//  - a bare vertical drag would have to be told apart from the existing
//    drag-and-drop by direction, and on a 3x3 grid "drag this block one row
//    up" is a NORMAL move — direction sensing would silently eat it, and
//    positions are load-bearing (recipes are position-sensitive). The rail
//    costs the move gesture nothing: it owns 12 px of a 64 px cell, and the
//    other 52 px still start a drag-and-drop in any direction;
//  - it is VISIBLE. A hidden gesture is undiscoverable for the beginner this
//    tab exists for; the rail shows the current mix on every block without
//    being touched, so "some of my blocks are turned down" is readable at a
//    glance and the control that did it is right there;
//  - no modifier key: modifiers are the least beginner-friendly option of
//    the three, and unreachable on a trackpad-only laptop workflow.
// It is still literally "a slider up and down on the block itself", which is
// what the producer asked for, and it darkens the block exactly as asked.
//
// Interaction contract (all of this is tested by eye in the checkpoints):
//   - plain click (rail or face) still SELECTS the cell; the weight only
//     moves once the pointer actually travels;
//   - right-click still CLEARS, rail included;
//   - while a material is ARMED, the whole cell (rail included) places it —
//     the guided click-cell-then-click-material flow is never dead anywhere;
//   - mouse wheel over a filled cell nudges the mix by 5 %;
//   - keyboard: UP/DOWN adjust the focused filled cell's mix (SHIFT = 25 %
//     jumps); LEFT/RIGHT walk the whole grid in reading order and wrap, so
//     every cell stays reachable without the vertical arrows. On an EMPTY
//     cell (nothing to mix) and on the BASE cell, UP/DOWN move a row as
//     before;
//   - empty cells have no rail, no tag, no readout.
// Weights never affect recipe detection or the auto-name (engine guarantee) —
// nothing in this UI implies otherwise.
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

// Mix rail geometry (sub-cell detail: 4-px grid, per PixelTheme's rule).
constexpr int kMixRailW      = 12;                // hit area on the right edge
constexpr int kMixTrackY     = 8;                 // track top inside the cell
constexpr int kMixTrackH     = 48;                // track height
constexpr int kMixHandleH    = 6;
constexpr float kMixStep     = 0.05f;             // rail/keyboard snap: 5 %
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

private:
    juce::Rectangle<int> railBounds() const;       // mix-rail hit area
    bool hasMixRail() const;                       // filled outer cell?
    void paintMixRail (juce::Graphics&, float weight01);
    void paintMixReadout (juce::Graphics&, float weight01);

    CraftGridComponent& grid;
    const int slot;                                // 0..7, or -1 for the base
    const bool baseSlot;
    bool hovering = false, dragOver = false, railHot = false;
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
    // Fired on every mix-rail / keyboard weight change (slot, 0..1). Kept
    // SEPARATE from onGridEdited on purpose: a weight edit must go through
    // the processor's setCraftCellWeight() path, which re-crafts WITHOUT
    // registering a discovery and without touching the recipe/auto name.
    std::function<void (int, float)> onCellWeightEdited;

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
    // Rail drag: snaps to kMixStep and HOLDS the big readout up until
    // endMixDrag(). No-op on an empty cell.
    void setCellWeightFromDrag (int slot, float weight01);
    void endMixDrag();
    // Keyboard / wheel: +-steps * kMixStep, readout shown for a moment.
    void nudgeCellWeight (int slot, int steps);
    // Which cell shows the big % readout right now (-1 = none).
    int mixReadoutSlot() const noexcept { return readoutSlot; }

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
