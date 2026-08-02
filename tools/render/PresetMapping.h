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

// Maps a preset JSON onto a ParamSnapshot. Since Phase 2 the ID list,
// ranges, clamping and value quantization all come from src/ParamSpec.h —
// the same table that builds the plugin's APVTS — so tools/render and the
// plugin produce identical snapshots for the same JSON.
//
// Since Phase 4, applyPreset() implements the full SPEC order: the "craft"
// grid is applied first (deterministic CraftEngine + recipe override from
// the shared recipe book), then "params" overrides on top. applyPresetParams
// remains as the params-only step.

#include <juce_core/juce_core.h>
#include "BlockwaveParams.h"
#include "ParamSpec.h"
#include "CraftJson.h"

namespace blockwave
{

inline bool applyPresetParams (const juce::var& presetRoot, ParamSnapshot& p, juce::String& error)
{
    auto* obj = presetRoot.getDynamicObject();
    if (obj == nullptr) { error = "preset root is not an object"; return false; }

    const auto paramsVar = obj->getProperty ("params");
    if (paramsVar.isVoid())
        return true;                         // no overrides: SPEC defaults
    auto* params = paramsVar.getDynamicObject();
    if (params == nullptr) { error = "\"params\" is not an object"; return false; }

    for (const auto& prop : params->getProperties())
    {
        PId id {};
        if (! findParam (prop.name.toString(), id))
            continue;                        // unknown IDs ignored (forward compat)
        applyToSnapshot (p, id, plainFromVar (id, prop.value));
    }
    return true;
}

// Full SPEC §Preset format application: craft first (when the preset carries
// a valid grid), then params on top. book may be null (no recipe overrides).
inline bool applyPreset (const juce::var& presetRoot, const RecipeBook* book,
                         ParamSnapshot& p, juce::String& error)
{
    auto* obj = presetRoot.getDynamicObject();
    if (obj == nullptr) { error = "preset root is not an object"; return false; }

    CraftGrid grid;
    if (craftGridFromVar (obj->getProperty ("craft"), grid))
        p = craftSnapshotWithRecipes (grid, book);
    return applyPresetParams (presetRoot, p, error);
}

} // namespace blockwave
