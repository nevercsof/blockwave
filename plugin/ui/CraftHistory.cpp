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

#include "CraftHistory.h"

namespace blockwave::ui
{

CraftState captureCraftState (BlockwaveAudioProcessor& proc)
{
    CraftState s;
    s.hasGrid = proc.getCraftGrid (s.grid);

    // The engine parameters as the APVTS holds them right now. Same mapping
    // the audio thread's RawParams::toSnapshot uses, so what we store is what
    // is actually being heard. Stops at kNumEngineParams: the craft MIX
    // weights have no snapshot field — they travel in the grid.
    for (int i = 0; i < blockwave::kNumEngineParams; ++i)
    {
        const auto id = static_cast<blockwave::PId> (i);
        if (const auto* raw = proc.apvts.getRawParameterValue (blockwave::paramDef (id).id))
            blockwave::applyToSnapshot (s.params, id, raw->load());
    }

    // "The grid may well match a recipe, but this sound is not it any more."
    // Only MUTATE produces that, and it is the one thing a re-craft cannot
    // reproduce on its own.
    s.recipeCleared = s.hasGrid && proc.getActiveRecipeName().isEmpty();
    return s;
}

void restoreCraftState (BlockwaveAudioProcessor& proc, const CraftState& s)
{
    // 1) The bench, through the single write path. setCraftGrid re-registers
    //    an already-found recipe as a no-op, so undo never re-fires the
    //    discovery toast, and it writes the weights THROUGH craft_mix_1..8,
    //    so the parameter stays the authority and the mirror cannot drift.
    if (s.hasGrid)
        proc.setCraftGrid (s.grid);
    else
        proc.clearCraftGrid();

    // 2) Re-suppress a recipe the sound had departed from. mutateCraft() is
    //    the only public call that clears the active recipe name; the offsets
    //    it throws at the parameters on the way out are overwritten wholesale
    //    by step 3, which is why the seed does not matter.
    if (s.recipeCleared && proc.getActiveRecipeName().isNotEmpty())
        proc.mutateCraft (0);

    // 3) The exact engine parameters. Identical to what the craft in step 1
    //    just wrote unless this state came from a MUTATE — in which case this
    //    is the only thing that can put it back. craft_mix is excluded by the
    //    function itself, so step 1 remains the authority on the weights.
    blockwave::applySnapshotToApvts (s.params, proc.apvts);
}

// ---- CraftHistory -----------------------------------------------------------

void CraftHistory::reset (const CraftState& current)
{
    first = 0;
    count = 1;
    cursor = 0;
    ring[0] = current;
    lastKind = Edit::none;
    lastSlot = -1;
    lastMs = 0;
}

void CraftHistory::rebase (const CraftState& current)
{
    if (count == 0)
    {
        reset (current);
        return;
    }
    at (cursor) = current;
}

void CraftHistory::commit (const CraftState& now, Edit kind, int slot,
                           juce::int64 atMs)
{
    if (count == 0)
    {
        reset (now);
        return;
    }

    // Coalescing: a run of 5 % nudges on one knob is one thing the user did,
    // so it is one undo step. Overwriting the cursor state (rather than
    // pushing) keeps the step's "before" exactly where the run started.
    if (kind == Edit::weight && lastKind == Edit::weight
        && slot >= 0 && slot == lastSlot
        && atMs - lastMs <= kCoalesceMs)
    {
        at (cursor) = now;
        lastMs = atMs;
        return;
    }

    count = cursor + 1;                        // a new step drops the redo tail

    if (count == kCapacity)                    // full: the oldest state goes
    {
        first = (first + 1) % kCapacity;
        --count;
        --cursor;
    }

    at (count) = now;
    ++count;
    cursor = count - 1;

    lastKind = kind;
    lastSlot = slot;
    lastMs = atMs;
}

const CraftState* CraftHistory::undo() noexcept
{
    if (! canUndo())
        return nullptr;
    --cursor;
    lastKind = Edit::none;                     // never coalesce onto a restore
    lastSlot = -1;
    return &at (cursor);
}

const CraftState* CraftHistory::redo() noexcept
{
    if (! canRedo())
        return nullptr;
    ++cursor;
    lastKind = Edit::none;
    lastSlot = -1;
    return &at (cursor);
}

} // namespace blockwave::ui
