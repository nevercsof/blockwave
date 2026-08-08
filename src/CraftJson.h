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

// Thin JUCE adapter around the pure craft engine (src/CraftEngine.h):
//  - CraftGrid <-> preset-JSON "craft" object ({"base":"PAD","cells":[...]})
//  - RecipeBook: the JSON recipe data (data/recipes.json, embedded via
//    BinaryData) — patterns + hand-tuned override patches. Schema documented
//    in docs/RECIPES_FORMAT.md.
//  - craftSnapshotWithRecipes(): the full deterministic craft: base +
//    materials, then (on an exact pattern match) the recipe override patch.
//    Used identically by the plugin and tools/render so both agree.
//  - DiscoveryStore: found-recipe persistence (JSON next to the user preset
//    folder). Message thread only.
//
// Everything here is message-thread / tool code. juce_core only.

#include <vector>
#include <juce_core/juce_core.h>
#include "CraftEngine.h"
#include "ParamSpec.h"

namespace blockwave
{

// The eight craft_mix_N parameters (src/ParamSpec.h) map 1:1 onto the eight
// grid cells. This is the one place both counts are visible together.
static_assert (kNumCraftMixParams == kNumCells,
               "one craft_mix parameter per grid cell, in reading order");

// ---- CraftGrid <-> juce::var ------------------------------------------------

// CRAFT JSON SHAPE (SPEC §Preset format):
//
//   "craft": {
//     "base": "PAD",
//     "cells":   ["ICE","ICE","ICE","","","","","CLOUD"],
//     "weights": [1, 1, 0.5, 1, 1, 1, 1, 0.25]        // OPTIONAL
//   }
//
// MIGRATION RULE (the whole reason "weights" is optional): a grid with no
// "weights" key — every factory preset, every user preset saved before this
// build, every hand-written JSON — reads as every placed cell at 1.0 (100%),
// which is bit-identical to the pre-weight engine. Nothing needs rewriting.
//
//  - values are 0..1 (1.0 = 100%); out-of-range values clamp, non-numeric
//    entries and missing trailing entries default to 1.0;
//  - a shorter array is legal (the tail defaults); a longer one is truncated;
//  - the weight of an EMPTY cell is meaningless: it is never compared
//    (CraftGrid::equalsWithWeights) and never decides whether the key is
//    written;
//  - the key is written only when at least one PLACED cell is off 1.0, so
//    saving an untouched craft still produces the exact same JSON as before.

// Parses a preset "craft" object. Returns false when there is no usable grid
// (missing/empty/unknown base — the SPEC INIT state). Unknown material names
// are treated as empty cells (forward compatibility).
inline bool craftGridFromVar (const juce::var& craftVar, CraftGrid& out)
{
    auto* obj = craftVar.getDynamicObject();
    if (obj == nullptr)
        return false;
    const auto baseStr = obj->getProperty ("base").toString();
    CraftBase base {};
    if (! baseFromName (baseStr.toRawUTF8(), base))
        return false;

    CraftGrid g;
    g.base = base;
    if (auto* cells = obj->getProperty ("cells").getArray())
    {
        for (int i = 0; i < kNumCells && i < cells->size(); ++i)
        {
            Material m = Material::none;
            materialFromName (cells->getUnchecked (i).toString().toRawUTF8(), m);
            g.cells[i] = m;
        }
    }
    if (auto* weights = obj->getProperty ("weights").getArray())
    {
        for (int i = 0; i < kNumCells && i < weights->size(); ++i)
        {
            const auto& v = weights->getUnchecked (i);
            if (v.isDouble() || v.isInt() || v.isInt64())
                g.setCellWeight (i, static_cast<float> (static_cast<double> (v)));
        }
    }
    out = g;
    return true;
}

inline juce::var craftGridToVar (const CraftGrid& g)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("base", juce::String (baseName (g.base)));
    juce::Array<juce::var> cells;
    for (int i = 0; i < kNumCells; ++i)
        cells.add (juce::String (materialName (g.cells[i])));
    obj->setProperty ("cells", cells);
    if (! g.allCellWeightsDefault())
    {
        juce::Array<juce::var> weights;
        for (int i = 0; i < kNumCells; ++i)
            weights.add (static_cast<double> (g.cellWeight (i)));
        obj->setProperty ("weights", weights);
    }
    return juce::var (obj);
}

// ---- Recipe book ------------------------------------------------------------

class RecipeBook
{
public:
    struct Recipe
    {
        juce::String name;       // ALL CAPS display name ("PERMAFROST")
        juce::String category;   // preset browser category hint
        CraftGrid pattern;       // position-sensitive exact pattern
        juce::var params;        // hand-tuned override patch ("params" object)
    };

    // Parses the recipe book JSON (docs/RECIPES_FORMAT.md). Replaces any
    // previous content. Returns false with error set on malformed input;
    // individual malformed recipes are skipped and reported in error too
    // (parse-what-you-can, so a bad added recipe never kills the book).
    bool loadFromJson (const juce::String& jsonText, juce::String& error)
    {
        error.clear();
        const auto root = juce::JSON::parse (jsonText);
        auto* obj = root.getDynamicObject();
        if (obj == nullptr)
        {
            error = "recipe book root is not an object";
            return false;
        }
        auto* list = obj->getProperty ("recipes").getArray();
        if (list == nullptr)
        {
            error = "recipe book has no \"recipes\" array";
            return false;
        }

        recipes.clear();
        for (const auto& rv : *list)
        {
            auto* r = rv.getDynamicObject();
            if (r == nullptr)
            {
                error += "skipped: recipe entry is not an object\n";
                continue;
            }
            Recipe rec;
            rec.name = r->getProperty ("name").toString();
            rec.category = r->getProperty ("category").toString();
            rec.params = r->getProperty ("params");
            CraftGrid g;
            if (rec.name.isEmpty() || ! craftGridFromVar (rv, g))
            {
                error += "skipped: recipe '" + rec.name + "' has no valid name/base/cells\n";
                continue;
            }
            rec.pattern = g;
            recipes.push_back (std::move (rec));
        }
        return true;
    }

    int getNumRecipes() const { return static_cast<int> (recipes.size()); }
    const Recipe& getRecipe (int i) const { return recipes[static_cast<size_t> (i)]; }

    // Position-sensitive exact match, first hit wins. nullptr = no match.
    const Recipe* match (const CraftGrid& g) const
    {
        for (const auto& r : recipes)
            if (r.pattern == g)
                return &r;
        return nullptr;
    }

    const Recipe* findByName (const juce::String& name) const
    {
        for (const auto& r : recipes)
            if (r.name.equalsIgnoreCase (name))
                return &r;
        return nullptr;
    }

private:
    std::vector<Recipe> recipes;
};

// ---- full craft (materials + recipe override) -------------------------------

// Applies a "params"-style object on top of a snapshot through the frozen
// SPEC table (same clamping/quantization as everywhere else).
inline void applyParamsVarToSnapshot (const juce::var& paramsVar, ParamSnapshot& p)
{
    auto* params = paramsVar.getDynamicObject();
    if (params == nullptr)
        return;
    for (const auto& prop : params->getProperties())
    {
        PId id {};
        if (findParam (prop.name.toString(), id))
            applyToSnapshot (p, id, plainFromVar (id, prop.value));
    }
}

// The complete deterministic craft: base archetype + material deltas, then —
// if the grid exactly matches a recipe pattern — that recipe's hand-tuned
// override patch. outRecipeName (optional) receives the matched name or "".
inline ParamSnapshot craftSnapshotWithRecipes (const CraftGrid& g,
                                               const RecipeBook* book,
                                               juce::String* outRecipeName = nullptr)
{
    ParamSnapshot p = craftApply (g);
    if (outRecipeName != nullptr)
        outRecipeName->clear();
    if (book != nullptr)
    {
        if (const auto* r = book->match (g))
        {
            applyParamsVarToSnapshot (r->params, p);
            if (outRecipeName != nullptr)
                *outRecipeName = r->name;
        }
    }
    return p;
}

// ---- Discoveries persistence ------------------------------------------------

// Found-recipe set, persisted as JSON in the BLOCKWAVE user folder (next to
// Presets/): ~/Documents/BLOCKWAVE/Discoveries.json. Message thread only.
// The folder/file are created lazily on the first discovery, never on load.
class DiscoveryStore
{
public:
    explicit DiscoveryStore (const juce::File& fileToUse) : file (fileToUse) { reload(); }

    static juce::File defaultFile()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("BLOCKWAVE").getChildFile ("Discoveries.json");
    }

    // Test/tool injection point; reloads from the new location.
    void setFile (const juce::File& f)
    {
        file = f;
        reload();
    }

    juce::File getFile() const { return file; }

    void reload()
    {
        found.clearQuick();
        if (! file.existsAsFile())
            return;
        const auto root = juce::JSON::parse (file.loadFileAsString());
        if (auto* list = root.getProperty ("found", juce::var()).getArray())
            for (const auto& v : *list)
            {
                const auto name = v.toString();
                if (name.isNotEmpty() && ! isFound (name))
                    found.add (name);
            }
    }

    bool isFound (const juce::String& name) const
    {
        for (const auto& n : found)
            if (n.equalsIgnoreCase (name))
                return true;
        return false;
    }

    // Returns true if this is a NEW discovery (and persists it). False when
    // already known or the name is empty.
    bool add (const juce::String& name)
    {
        if (name.isEmpty() || isFound (name))
            return false;
        found.add (name);
        save();
        return true;
    }

    int getNumFound() const { return found.size(); }
    const juce::StringArray& getFoundNames() const { return found; }

private:
    void save()
    {
        auto dir = file.getParentDirectory();
        if (! dir.isDirectory() && dir.createDirectory().failed())
            return;                              // non-fatal: stays in memory
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("formatVersion", 1);
        juce::Array<juce::var> list;
        for (const auto& n : found)
            list.add (n);
        obj->setProperty ("found", list);
        file.replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    juce::File file;
    juce::StringArray found;
};

} // namespace blockwave
