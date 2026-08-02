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

// Maps a preset JSON "params" object onto a ParamSnapshot. Since Phase 2 the
// ID list, ranges, clamping and value quantization all come from
// src/ParamSpec.h — the same table that builds the plugin's APVTS — so
// tools/render and the plugin produce identical snapshots for the same JSON.
// FX parameters (crush/dly/cave) are accepted, validated and stored in the
// APVTS but have no engine fields until Phase 5.

#include <juce_core/juce_core.h>
#include "BlockwaveParams.h"
#include "ParamSpec.h"

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

} // namespace blockwave
