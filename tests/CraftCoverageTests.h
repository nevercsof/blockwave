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

// Phase-4 follow-up tests (included by tests/StateTests.cpp, so they inherit
// its strict-warning flags and its JUCE/BinaryData link):
//
//  1. recipe_book_coverage — DATA-DRIVEN over whatever the shipped recipe book
//     contains. Adding recipe 17 must need no edit here.
//  2. craft_transition_click_free — CRAFT_GRID.md #4: "audible params glide
//     over ~30 ms (no clicks)". A matrix of mid-note grid transitions, gated
//     like test_param_smoothing_click_free in tests/TestMain.cpp, with a
//     hard-splice negative control proving the detector bites.

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "PluginProcessor.h"
#include "RecipeData.h"
#include "TestUtil.h"

namespace craftcoverage
{

using namespace blockwave;
using testutil::state;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Grid with `count` copies of `fill` in the first cells (cell order 0..7,
// see the CELL INDEXING note in src/CraftEngine.h).
inline CraftGrid gridOf (CraftBase base, Material fill = Material::none, int count = 0)
{
    CraftGrid g;
    g.base = base;
    for (int i = 0; i < count && i < kNumCells; ++i)
        g.cells[i] = fill;
    return g;
}

inline juce::File repoDataFile (const char* leaf)
{
    return juce::File (BLOCKWAVE_TEST_DIR).getParentDirectory()
               .getChildFile ("data").getChildFile (leaf);
}

// ---------------------------------------------------------------------------
// 1. recipe_book_coverage (defect B)
//
// Everything below iterates the book itself — never a hardcoded count — so a
// "Recipe Update" that ships more recipes is covered automatically.
// ---------------------------------------------------------------------------
inline void test_recipe_book_coverage (BlockwaveAudioProcessor& proc)
{
    std::printf ("[recipe_book_coverage]\n");

    // ---- (a) parses from disk AND from the embedded BinaryData, byte-identical
    const auto diskFile = repoDataFile ("recipes.json");
    CHECK_MSG (diskFile.existsAsFile(), "data/recipes.json not found at %s",
               diskFile.getFullPathName().toRawUTF8());
    if (! diskFile.existsAsFile())
        return;

    juce::MemoryBlock diskBytes;
    CHECK_MSG (diskFile.loadFileAsData (diskBytes), "could not read data/recipes.json");

    int embeddedSize = 0;
    const char* embedded = RecipeData::getNamedResource ("recipes_json", embeddedSize);
    CHECK_MSG (embedded != nullptr, "BinaryData resource 'recipes_json' missing");
    if (embedded == nullptr)
        return;

    CHECK_MSG (static_cast<size_t> (embeddedSize) == diskBytes.getSize(),
               "recipes.json size differs: disk %d bytes, BinaryData %d bytes "
               "(stale build? re-run CMake)",
               static_cast<int> (diskBytes.getSize()), embeddedSize);
    if (static_cast<size_t> (embeddedSize) == diskBytes.getSize())
        CHECK_MSG (std::memcmp (diskBytes.getData(), embedded,
                                static_cast<size_t> (embeddedSize)) == 0,
                   "recipes.json on disk differs byte-for-byte from BinaryData");

    RecipeBook fromDisk, fromBinary;
    juce::String errDisk, errBinary;
    CHECK_MSG (fromDisk.loadFromJson (diskFile.loadFileAsString(), errDisk),
               "disk recipe book failed to parse");
    CHECK_MSG (errDisk.isEmpty(), "disk recipe book skipped entries: %s",
               errDisk.toRawUTF8());
    CHECK_MSG (fromBinary.loadFromJson (
                   juce::String::fromUTF8 (embedded, embeddedSize), errBinary),
               "BinaryData recipe book failed to parse");
    CHECK_MSG (errBinary.isEmpty(), "BinaryData recipe book skipped entries: %s",
               errBinary.toRawUTF8());

    const int n = fromDisk.getNumRecipes();
    CHECK_MSG (n == fromBinary.getNumRecipes(),
               "recipe count differs: disk %d, BinaryData %d",
               n, fromBinary.getNumRecipes());
    CHECK_MSG (n == proc.getRecipeBook().getNumRecipes(),
               "processor book has %d recipes, file has %d",
               proc.getRecipeBook().getNumRecipes(), n);

    // Every parsed recipe must agree field-for-field across the two sources.
    const int shared = std::min (n, fromBinary.getNumRecipes());
    for (int i = 0; i < shared; ++i)
    {
        const auto& a = fromDisk.getRecipe (i);
        const auto& b = fromBinary.getRecipe (i);
        CHECK_MSG (a.name == b.name, "recipe %d name differs: '%s' vs '%s'", i,
                   a.name.toRawUTF8(), b.name.toRawUTF8());
        CHECK_MSG (a.pattern == b.pattern, "'%s': pattern differs disk vs BinaryData",
                   a.name.toRawUTF8());
        CHECK_MSG (juce::JSON::toString (a.params) == juce::JSON::toString (b.params),
                   "'%s': override params differ disk vs BinaryData", a.name.toRawUTF8());
    }

    // ---- (b) no duplicate patterns
    int duplicates = 0;
    juce::String duplicateDetail;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (fromDisk.getRecipe (i).pattern == fromDisk.getRecipe (j).pattern)
            {
                ++duplicates;
                duplicateDetail << " '" << fromDisk.getRecipe (i).name << "'=='"
                                << fromDisk.getRecipe (j).name << "'";
            }
    CHECK_MSG (duplicates == 0, "%d duplicate recipe pattern(s):%s",
               duplicates, duplicateDetail.toRawUTF8());

    // ---- (c) every recipe is reachable through match() — no shadowing
    // ---- (d) every single-cell near-miss fails to match that recipe
    // ---- (e) every override actually changes the plain craft result
    int nearMissChecks = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto& rec = fromDisk.getRecipe (i);
        const char* nm = rec.name.toRawUTF8();

        const auto* hit = fromDisk.match (rec.pattern);
        CHECK_MSG (hit == &rec,
                   "'%s' is unreachable: match() returned %s (shadowed by an "
                   "earlier first-hit?)",
                   nm, hit == nullptr ? "no recipe" : hit->name.toRawUTF8());

        CHECK_MSG (rec.name.isNotEmpty(), "recipe %d has an empty name", i);

        // The override must move at least one parameter off the plain craft.
        const auto plain = craftApply (rec.pattern);
        auto overridden = plain;
        applyParamsVarToSnapshot (rec.params, overridden);
        CHECK_MSG (hashSnapshot (plain) != hashSnapshot (overridden),
                   "'%s': override patch changes nothing vs plain craft", nm);

        // A material to fill empty cells with, so a near-miss stays "one cell
        // away" rather than introducing an unrelated material.
        Material filler = Material::none;
        for (int c = 0; c < kNumCells; ++c)
            if (rec.pattern.cells[c] != Material::none)
            {
                filler = rec.pattern.cells[c];
                break;
            }
        CHECK_MSG (filler != Material::none, "'%s': pattern has no materials", nm);
        if (filler == Material::none)
            continue;

        for (int c = 0; c < kNumCells; ++c)
        {
            CraftGrid near = rec.pattern;
            near.cells[c] = near.cells[c] == Material::none ? filler : Material::none;
            const auto* nearHit = fromDisk.match (near);
            CHECK_MSG (nearHit != &rec,
                       "'%s': toggling cell %d still matches it — the pattern is "
                       "not position/content exact", nm, c);
            ++nearMissChecks;
            // Landing on a DIFFERENT recipe would mean two recipes are one cell
            // apart; legal, but worth surfacing.
            if (nearHit != nullptr && nearHit != &rec)
                std::printf ("    note: '%s' cell %d toggled lands on '%s'\n",
                             nm, c, nearHit->name.toRawUTF8());
        }
    }

    std::printf ("  %d recipes: disk == BinaryData (%d bytes), 0 duplicate patterns,\n"
                 "  all reachable via match(), %d single-cell near-misses rejected,\n"
                 "  every override changes the craft result\n",
                 n, embeddedSize, nearMissChecks);
}

// ---------------------------------------------------------------------------
// 2. craft_transition_click_free (defect A)
// ---------------------------------------------------------------------------

// Mid-note render: patch A, swap to patch B at swapSec, keep rendering.
// The swap lands on an exact sample index (the block is split), so the result
// is independent of the host block size.
inline std::vector<float> renderSwap (const ParamSnapshot& a, const ParamSnapshot& b,
                                      int note, float vel, double sr, int block,
                                      double swapSec, double tailSec, int& swapSample)
{
    const int total  = static_cast<int> ((swapSec + tailSec) * sr);
    const int stepAt = static_cast<int> (swapSec * sr);
    swapSample = stepAt;

    BlockwaveEngine e;
    e.setParams (a);
    e.setTempo (120.0);
    e.prepare (sr, block);
    e.noteOn (note, vel);

    std::vector<float> l (static_cast<size_t> (total)), r (static_cast<size_t> (total));
    bool swapped = false;
    for (int pos = 0; pos < total; )
    {
        if (! swapped && pos >= stepAt) { e.setParams (b); swapped = true; }
        int n = std::min (block, total - pos);
        if (! swapped && pos + n > stepAt)
            n = stepAt - pos;
        e.process (l.data() + pos, r.data() + pos, n);
        pos += n;
    }
    return l;
}

inline float maxStep (const std::vector<float>& x, int from, int to)
{
    float m = 0.0f;
    const int lo = std::max (1, from);
    const int hi = std::min (static_cast<int> (x.size()), to);
    for (int i = lo; i < hi; ++i)
        m = std::max (m, std::fabs (x[static_cast<size_t> (i)]
                                  - x[static_cast<size_t> (i - 1)]));
    return m;
}

struct TransitionScore
{
    float swap = 0.0f;      // worst single-sample step in the 30 ms swap window
    float bound = 0.0f;     // 1.25 x max(steady before, steady after)
    float ratio = 0.0f;
    double at = 0.0;        // swap instant that produced the worst ratio
};

// Worst case over a fixed set of swap instants: the click is phase dependent
// (LFO1 PWM phase, oscillator phase, envelope stage), so a single instant can
// miss it. The instants are fixed constants — the test stays deterministic.
inline TransitionScore scoreTransition (const ParamSnapshot& a, const ParamSnapshot& b,
                                        int note, float vel, double sr, int block)
{
    constexpr double kInstants[] = { 0.42, 0.70, 0.99, 1.28, 1.57, 1.86 };
    TransitionScore worst;
    for (const double t : kInstants)
    {
        int stepAt = 0;
        const auto l = renderSwap (a, b, note, vel, sr, block, t, 0.45, stepAt);
        const float swap   = maxStep (l, stepAt, stepAt + static_cast<int> (0.030 * sr));
        const float before = maxStep (l, stepAt - static_cast<int> (0.150 * sr), stepAt);
        const float after  = maxStep (l, stepAt + static_cast<int> (0.200 * sr),
                                      static_cast<int> (l.size()));
        const float bound  = 1.25f * std::max (before, after);
        const float ratio  = swap / std::max (1.0e-9f, bound);
        if (ratio > worst.ratio)
            worst = { swap, bound, ratio, t };
    }
    return worst;
}

inline void test_craft_transition_click_free (BlockwaveAudioProcessor& proc)
{
    std::printf ("[craft_transition_click_free]\n");

    const auto& book = proc.getRecipeBook();
    const int note = 60;
    const float vel = 100.0f / 127.0f;

    struct Case { const char* label; CraftGrid from, to; };
    const CraftGrid pad      = gridOf (CraftBase::PAD);
    const CraftGrid padIce   = gridOf (CraftBase::PAD, Material::ICE, 1);
    const CraftGrid padStone = gridOf (CraftBase::PAD, Material::STONE, 1);
    const CraftGrid padSand  = gridOf (CraftBase::PAD, Material::SAND, 1);
    const CraftGrid padCryst = gridOf (CraftBase::PAD, Material::CRYSTAL, 1);
    const CraftGrid padLava  = gridOf (CraftBase::PAD, Material::LAVA, 1);
    const CraftGrid padObs8  = gridOf (CraftBase::PAD, Material::OBSIDIAN, 8);  // SINKHOLE
    const CraftGrid bass     = gridOf (CraftBase::BASS);
    const CraftGrid bassLava = gridOf (CraftBase::BASS, Material::LAVA, 1);
    const CraftGrid keys     = gridOf (CraftBase::KEYS);
    const CraftGrid lead     = gridOf (CraftBase::LEAD);
    const CraftGrid drone    = gridOf (CraftBase::DRONE);

    const Case cases[] =
    {
        { "material added (PAD -> PAD+ICE)",      pad,     padIce   },
        { "material removed (PAD+ICE -> PAD)",    padIce,  pad      },
        { "RAW on (PAD -> PAD+STONE)",            pad,     padStone },
        { "RAW off (PAD+STONE -> PAD)",           padStone, pad     },
        { "noise on (PAD -> PAD+SAND)",           pad,     padSand  },
        { "sync on (PAD -> PAD+CRYSTAL)",         pad,     padCryst },
        { "crush on (PAD -> PAD+LAVA)",           pad,     padLava  },
        { "crush off (PAD+LAVA -> PAD)",          padLava, pad      },
        { "+/-2 oct (PAD -> PAD+OBSIDIANx8)",     pad,     padObs8  },
        { "base swap PAD -> KEYS",                pad,     keys     },
        { "base swap LEAD -> BASS",               lead,    bass     },
        { "base swap PAD -> DRONE",               pad,     drone    },
        { "base swap PAD -> BASS+LAVA",           pad,     bassLava },
        // The two Phase-4 regressions: source is the SINKHOLE recipe, whose
        // override parks ENV2 at sustain 1.0 with filt_env -0.7.
        { "REGRESSION PAD+OBSIDIANx8 -> BASS",      padObs8, bass     },
        { "REGRESSION PAD+OBSIDIANx8 -> BASS+LAVA", padObs8, bassLava },
    };

    // Gate. The 13 non-regression transitions here score 0.62..1.48; the two
    // Phase-4 regressions scored ~25x and ~29x this gate before the fix, so
    // 2.5 leaves comfortable headroom over normal patch-to-patch variation
    // while staying far below anything the defect could produce.
    constexpr float kMaxRatio = 2.5f;

    float worstClean = 0.0f, worstOverall = 0.0f;
    for (const auto& c : cases)
    {
        const auto a = craftSnapshotWithRecipes (c.from, &book);
        const auto b = craftSnapshotWithRecipes (c.to, &book);
        const auto s = scoreTransition (a, b, note, vel, 48000.0, 512);
        std::printf ("  %-42s step %.4f bound %.4f  ratio %5.2f (t=%.2f s)\n",
                     c.label, static_cast<double> (s.swap),
                     static_cast<double> (s.bound), static_cast<double> (s.ratio), s.at);
        CHECK_MSG (s.ratio <= kMaxRatio,
                   "%s: click — swap step %.4f is %.2fx the steady bound %.4f",
                   c.label, static_cast<double> (s.swap),
                   static_cast<double> (s.ratio), static_cast<double> (s.bound));
        worstOverall = std::max (worstOverall, s.ratio);
        if (std::strncmp (c.label, "REGRESSION", 10) != 0)
            worstClean = std::max (worstClean, s.ratio);
    }

    // The two regressions also reproduced at 44.1 kHz and at block 64 — cover
    // both here so a fix that only holds at 48k/512 cannot pass. Selected by
    // label, so reordering or extending the matrix above stays safe.
    for (const auto& c : cases)
    {
        if (std::strncmp (c.label, "REGRESSION", 10) != 0)
            continue;
        const auto a = craftSnapshotWithRecipes (c.from, &book);
        const auto b = craftSnapshotWithRecipes (c.to, &book);
        for (const auto rateBlock : { std::pair<double, int> { 44100.0, 64 },
                                      std::pair<double, int> { 44100.0, 512 },
                                      std::pair<double, int> { 48000.0, 64 } })
        {
            const auto s = scoreTransition (a, b, note, vel, rateBlock.first, rateBlock.second);
            std::printf ("  %-42s ratio %5.2f  @ %.0f Hz / %d\n", c.label,
                         static_cast<double> (s.ratio), rateBlock.first, rateBlock.second);
            CHECK_MSG (s.ratio <= kMaxRatio,
                       "%s @ %.0f/%d: click — swap step %.4f is %.2fx bound %.4f",
                       c.label, rateBlock.first, rateBlock.second,
                       static_cast<double> (s.swap), static_cast<double> (s.ratio),
                       static_cast<double> (s.bound));
        }
    }

    // ---- negative control -------------------------------------------------
    // Splice two independent steady renders at the swap point: that is exactly
    // what an unsmoothed parameter jump does to the waveform. It must blow
    // through the same gate, otherwise the detector above proves nothing.
    {
        const auto a = craftSnapshotWithRecipes (padObs8, &book);
        const auto b = craftSnapshotWithRecipes (bassLava, &book);
        const double sr = 48000.0;
        const double swapSec = 0.99;
        const int stepAt = static_cast<int> (swapSec * sr);

        auto ra = testutil::renderNote (a, note, sr, 2.0, 1.6, 512, 120.0, vel);
        auto rb = testutil::renderNote (b, note, sr, 2.0, 1.6, 512, 120.0, vel);

        // Steady references come from the two renders themselves.
        const float before = maxStep (ra.l, stepAt - static_cast<int> (0.150 * sr), stepAt);
        const float after  = maxStep (rb.l, stepAt + static_cast<int> (0.200 * sr),
                                      static_cast<int> (rb.l.size()));
        const float bound  = 1.25f * std::max (before, after);

        // Worst splice over one full 55 Hz-ish period: whether a given splice
        // instant lands on a big discontinuity is pure phase luck, and a
        // detector that only catches the lucky ones proves nothing.
        float swap = 0.0f;
        for (int k = 0; k < 900; ++k)
        {
            const size_t n0 = static_cast<size_t> (stepAt + k);
            swap = std::max (swap, std::fabs (rb.l[n0] - ra.l[n0 - 1]));
        }
        const float ratio = swap / std::max (1.0e-9f, bound);
        std::printf ("  negative control (hard splice)             step %.4f bound %.4f"
                     "  ratio %5.2f\n", static_cast<double> (swap),
                     static_cast<double> (bound), static_cast<double> (ratio));
        CHECK_MSG (ratio > kMaxRatio,
                   "click detector is toothless: a hard splice only scored %.2f "
                   "against the %.2f gate", static_cast<double> (ratio),
                   static_cast<double> (kMaxRatio));
    }

    std::printf ("  %d transitions clean (gate %.1fx); worst non-regression %.2f, "
                 "worst overall %.2f\n",
                 static_cast<int> (sizeof (cases) / sizeof (cases[0])),
                 static_cast<double> (kMaxRatio),
                 static_cast<double> (worstClean), static_cast<double> (worstOverall));
}

} // namespace craftcoverage
