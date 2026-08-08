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

// UNDO / REDO for the crafting bench (producer request: "обязательно
// кнопочки undo redo в фирменном стиле"). Message thread only.
//
// ---- SCOPE: THE BENCH, AND ONLY THE BENCH ----------------------------------
// Undoable: place a block, clear a cell, swap two cells, cycle the base, move
// a MIX knob, DICE, MUTATE, CLEAR. That is the list of gestures the CRAFT tab
// owns.
//
// NOT undoable, deliberately:
//   - a PRESET LOAD. Someone who loads a preset and hits undo wants their
//     bench back, not to be dumped into the previous preset's parameters —
//     and the moment undo starts crossing into preset territory it is
//     competing with the host's own undo over the same parameters. Loading a
//     preset therefore CLEARS the stack: new context, clean slate.
//   - host automation and host state restore, for the same reason.
//   - anything on the TWEAK tab. (Note this costs nothing in practice: every
//     bench edit already recomputes all 67 engine parameters from the craft,
//     so a manual tweak does not survive the NEXT bench edit either.)
//
// The stack is pure UI/session state. It is never serialised — not into a
// preset, not into the plugin's session state — and it starts empty every
// time the editor is opened.
//
// ---- WHAT A STATE IS -------------------------------------------------------
// The grid alone is not enough, because MUTATE moves the parameters WITHOUT
// moving the grid: undoing one MUTATE by re-crafting the grid would also
// throw away the previous MUTATE. So a state is the grid PLUS the engine
// parameters it was heard with:
//
//   grid            cells + base + the eight cell weights;
//   params          all 67 engine parameters (ParamSnapshot);
//   hasGrid         false for the genuinely uncrafted patch;
//   recipeCleared   the grid matches a recipe but the active recipe name was
//                   empty — i.e. a MUTATE has taken the sound away from it.
//
// ---- HOW A STATE IS RESTORED ------------------------------------------------
// Through the processor's public API only, never by poking its members:
//
//   1. setCraftGrid()  — THE single write path (PluginProcessor.h): grid,
//      craft JSON, the craft_mix parameters and the craft's own parameter
//      write all happen there, so the APVTS-authoritative weights and the
//      grid mirror cannot drift apart. (An already-found recipe re-registers
//      as a no-op, so an undo never re-fires the discovery toast.)
//   2. mutateCraft() — ONLY when recipeCleared says the state had departed
//      from its recipe. It is the one public call that clears the active
//      recipe name; the parameters it scrambles on the way are put back
//      exactly by step 3, which runs after it.
//   3. applySnapshotToApvts() — the exact engine parameters. This is the same
//      function applyCraftGrid() uses to write a craft, and it deliberately
//      does not touch craft_mix, so step 1 stays the authority on weights.

#include "../PluginProcessor.h"

#include <array>

namespace blockwave::ui
{

struct CraftState
{
    blockwave::CraftGrid grid;
    blockwave::ParamSnapshot params;
    bool hasGrid = false;
    bool recipeCleared = false;
};

// Reads the processor's current bench + engine parameters. Message thread.
CraftState captureCraftState (BlockwaveAudioProcessor&);

// Puts one back, through the write path described above. Message thread.
void restoreCraftState (BlockwaveAudioProcessor&, const CraftState&);

// Fixed-size ring of states with a cursor. It cannot grow: the oldest state
// is dropped once the ring is full, so a long session costs a constant
// ~13 KB and never allocates after construction.
class CraftHistory
{
public:
    // 32 undo steps means 33 stored states (the baseline plus one per step).
    static constexpr int kMaxUndoSteps = 32;
    static constexpr int kCapacity     = kMaxUndoSteps + 1;

    // A run of nudges on the SAME knob within this window is one undo step.
    // A knob DRAG does not rely on it at all — the drag commits once, at
    // mouse-up (see CraftTab) — this only coalesces the wheel and the arrow
    // keys, which arrive as a stream of separate 5 % edits.
    static constexpr int kCoalesceMs = 800;

    enum class Edit { none, grid, weight, dice, mutation };

    // Throws the history away and makes `current` the baseline. Editor open,
    // preset load.
    void reset (const CraftState& current);

    // Replaces the state the cursor points at without adding a step. Used
    // when the patch moved without a bench edit (host automation picked up by
    // the resync poll) and at the start of a knob drag, so the state the drag
    // will undo to is the one that was actually on screen.
    void rebase (const CraftState& current);

    // Adds `now` as a new step, dropping any redo tail. `slot` is the cell a
    // weight edit belongs to (-1 otherwise) and `atMs` a millisecond clock.
    void commit (const CraftState& now, Edit kind, int slot, juce::int64 atMs);

    bool canUndo() const noexcept { return cursor > 0; }
    bool canRedo() const noexcept { return count > 0 && cursor + 1 < count; }

    // Move the cursor and hand back the state to restore, or nullptr.
    const CraftState* undo() noexcept;
    const CraftState* redo() noexcept;

    int getNumStates()    const noexcept { return count; }
    int getNumUndoSteps() const noexcept { return cursor; }
    int getNumRedoSteps() const noexcept { return count > 0 ? count - cursor - 1 : 0; }

private:
    CraftState& at (int i) noexcept
    {
        return ring[static_cast<size_t> ((first + i) % kCapacity)];
    }

    std::array<CraftState, kCapacity> ring {};
    int first = 0, count = 0, cursor = 0;

    Edit lastKind = Edit::none;
    int lastSlot = -1;
    juce::int64 lastMs = 0;
};

} // namespace blockwave::ui
