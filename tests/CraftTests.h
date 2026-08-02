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

// Phase-4 CRAFT engine tests. Included by TestMain.cpp (same strict TU).
// Pure C++ — exercises src/CraftEngine.h without JUCE:
//
//  - shapeless crafting + diminishing-stack math (exact expected values)
//  - fixed-order conflict resolution (VOLT/SLIME/MOSS lfo2, STONE raw)
//  - determinism golden hashes (DoD): fixed grids -> frozen snapshot hashes
//  - recipe pattern matching: all 8 spec patterns hit, perturbations miss
//  - auto-naming: adjective order, ties, cap, spec examples
//  - DICE / MUTATE: seed-deterministic, base kept, ranges respected
//  - material distinctness matrix (DoD proxy for "audibly distinct on every
//    base"): for all 8 bases x 14 materials, base+material must differ from
//    the bare base by a meaningful margin in at least one of four metrics:
//      rmsDelta   — relative full-render RMS change (levels, mix, envelope)
//      centShift  — spectral centroid shift in octaves (brightness: cutoff,
//                   PW, sub/oct changes)
//      specDist   — cosine distance between sqrt-compressed magnitude
//                   spectra (timbre/texture: noise, crush, sync, unison)
//      tailDelta  — relative RMS change after note-off (release, cave, delay)
//    Together these four cover every delta class a material can apply; a
//    render that moves none of them beyond threshold would be very hard to
//    tell apart by ear, which makes the union a fair automated proxy.

#include <cstdlib>
#include <initializer_list>
#include <string>

#include "CraftEngine.h"

namespace crafttests
{

using namespace blockwave;
using namespace testutil;

// ---------------------------------------------------------------------------
static CraftGrid makeGrid (CraftBase b,
                           std::initializer_list<Material> mats = {})
{
    CraftGrid g;
    g.base = b;
    int i = 0;
    for (const auto m : mats)
        if (i < kNumCells)
            g.cells[i++] = m;
    return g;
}

static bool snapshotsEqual (const ParamSnapshot& a, const ParamSnapshot& b)
{
    return hashSnapshot (a) == hashSnapshot (b);
}

// ---------------------------------------------------------------------------
static void test_craft_shapeless_and_stacking()
{
    std::printf ("[craft_shapeless_and_stacking]\n");

    // Shapeless: same multiset in different cells => identical snapshot.
    {
        CraftGrid a = makeGrid (CraftBase::LEAD, { Material::ICE, Material::GOLD });
        CraftGrid b;
        b.base = CraftBase::LEAD;
        b.cells[3] = Material::GOLD;
        b.cells[7] = Material::ICE;
        CHECK_MSG (snapshotsEqual (craftApply (a), craftApply (b)),
                   "shapeless violated: position changed a non-recipe craft");
    }

    // Diminishing stacking, exact math. PAD base env1_r = 1.1.
    {
        const auto p0 = craftApply (makeGrid (CraftBase::PAD));
        const auto p1 = craftApply (makeGrid (CraftBase::PAD, { Material::ICE }));
        const auto p2 = craftApply (makeGrid (CraftBase::PAD, { Material::ICE, Material::ICE }));

        // mul x2.5: copy 1 full, copy 2 at half strength (x1.75).
        const float r0 = p0.env1_r;
        CHECK_MSG (std::abs (p1.env1_r - r0 * 2.5f) < 1.0e-5f,
                   "ICE x1 release: %.4f != %.4f", (double) p1.env1_r, (double) (r0 * 2.5f));
        CHECK_MSG (std::abs (p2.env1_r - r0 * 2.5f * 1.75f) < 1.0e-5f,
                   "ICE x2 release: %.4f != %.4f", (double) p2.env1_r,
                   (double) (r0 * 2.5f * 1.75f));

        // add +2 uni at weights 1.0 / 1.5 on PAD's 5.
        CHECK_MSG (p1.uni_count == 7, "ICE x1 uni_count %d != 7", p1.uni_count);
        CHECK_MSG (p2.uni_count == 8, "ICE x2 uni_count %d != 8 (5+3)", p2.uni_count);

        // set PW -> 38 does not intensify with stacking.
        CHECK_MSG (p1.oscA_pw == 38.0f && p2.oscA_pw == 38.0f,
                   "ICE PW set drifted: %.2f / %.2f",
                   (double) p1.oscA_pw, (double) p2.oscA_pw);

        // Clamps hold under a full stack of 8.
        const auto p8 = craftApply (makeGrid (CraftBase::PAD,
            { Material::ICE, Material::ICE, Material::ICE, Material::ICE,
              Material::ICE, Material::ICE, Material::ICE, Material::ICE }));
        CHECK_MSG (p8.uni_count == 8, "ICE x8 uni_count clamp: %d", p8.uni_count);
        CHECK_MSG (p8.filt_cutoff <= 20000.0f && p8.cave_mix <= 1.0f,
                   "ICE x8 exceeded SPEC ranges");
    }

    // Fixed-order conflicts: later material in table order wins lfo2 fields.
    {
        const auto voltSlime = craftApply (makeGrid (CraftBase::LEAD,
                                           { Material::VOLT, Material::SLIME }));
        CHECK_MSG (voltSlime.lfo2_dest == Lfo2Dest::pw,
                   "VOLT+SLIME: SLIME (later) must own lfo2_dest");
        CHECK_MSG (voltSlime.lfo2_shape == LfoShape::tri,
                   "VOLT+SLIME: SLIME sets tri");
        // Both amounts stack additively regardless: 0.3 + 0.4.
        CHECK_MSG (std::abs (voltSlime.lfo2_amt - 0.7f) < 1.0e-5f,
                   "VOLT+SLIME lfo2_amt %.3f != 0.7", (double) voltSlime.lfo2_amt);

        const auto slimeMoss = craftApply (makeGrid (CraftBase::LEAD,
                                           { Material::SLIME, Material::MOSS }));
        CHECK_MSG (slimeMoss.lfo2_dest == Lfo2Dest::pitch,
                   "SLIME+MOSS: MOSS (later) must own lfo2_dest");

        // STONE's raw wins last; switches only turn ON.
        const auto stoneMix = craftApply (makeGrid (CraftBase::CHIP,
                                          { Material::STONE, Material::ICE,
                                            Material::CLOUD }));
        CHECK_MSG (stoneMix.raw, "STONE raw lost in a mixed grid");
        // STONE cave_mix 0 applied at table position 3; CLOUD adds after.
        CHECK_MSG (stoneMix.cave_mix > 0.0f,
                   "fixed order broken: CLOUD (after STONE) must re-add cave");
    }

    // Discrete switches: CRYSTAL turns B on + sync; SAND/TNT turn noise on.
    {
        const auto c = craftApply (makeGrid (CraftBase::KEYS, { Material::CRYSTAL }));
        CHECK_MSG (c.oscB_on && c.oscB_sync && c.oscB_semi == 7,
                   "CRYSTAL switch set broken");
        const auto s = craftApply (makeGrid (CraftBase::KEYS, { Material::SAND }));
        CHECK_MSG (s.noise_on && s.noise_mode == NoiseMode::longMode,
                   "SAND switch set broken");
        const auto o = craftApply (makeGrid (CraftBase::KEYS, { Material::OBSIDIAN }));
        CHECK_MSG (o.sub_on && o.oscA_oct == -1, "OBSIDIAN switch/oct broken");
    }
}

// ---------------------------------------------------------------------------
// Determinism golden hashes (DoD). The values below are FROZEN: they were
// generated once from the tuned tables and any change to base archetypes,
// material deltas or application order must consciously regenerate them
// (run with BLOCKWAVE_PRINT_CRAFT_HASHES=1 in the environment to reprint).
// ---------------------------------------------------------------------------

struct GoldenGrid { const char* label; CraftGrid grid; std::uint64_t hash; };

static std::vector<GoldenGrid> goldenGrids()
{
    using M = Material;
    std::vector<GoldenGrid> v;

    // Empty grid per base.
    v.push_back ({ "LEAD_empty",  makeGrid (CraftBase::LEAD),  0x3662bf909c0ce82bULL });
    v.push_back ({ "BASS_empty",  makeGrid (CraftBase::BASS),  0x13ec17bd1dd9654cULL });
    v.push_back ({ "PAD_empty",   makeGrid (CraftBase::PAD),   0xe11a67beedf1d039ULL });
    v.push_back ({ "PLUCK_empty", makeGrid (CraftBase::PLUCK), 0xe2a9195cd9c77452ULL });
    v.push_back ({ "KEYS_empty",  makeGrid (CraftBase::KEYS),  0xfb6e793fb7508ab6ULL });
    v.push_back ({ "CHIP_empty",  makeGrid (CraftBase::CHIP),  0x31e0f5880f2c6282ULL });
    v.push_back ({ "PERC_empty",  makeGrid (CraftBase::PERC),  0x6c48897a520ed8c5ULL });
    v.push_back ({ "DRONE_empty", makeGrid (CraftBase::DRONE), 0x0139b1033ad668b3ULL });

    // Stacked + mixed grids.
    v.push_back ({ "PAD_ICE",     makeGrid (CraftBase::PAD, { M::ICE }),
                   0xe5c25ae4434a5a1fULL });
    v.push_back ({ "PAD_ICEx4",   makeGrid (CraftBase::PAD,
                   { M::ICE, M::ICE, M::ICE, M::ICE }), 0x384bc0b47ce48285ULL });
    v.push_back ({ "LEAD_LAVA_GLASS", makeGrid (CraftBase::LEAD,
                   { M::LAVA, M::GLASS }), 0x63b2470c07d4dab7ULL });
    v.push_back ({ "BASS_mix8",   makeGrid (CraftBase::BASS,
                   { M::ICE, M::LAVA, M::STONE, M::WOOD,
                     M::GLASS, M::GOLD, M::CRYSTAL, M::VOLT }),
                   0xa670095784a7f324ULL });
    v.push_back ({ "CHIP_mix6",   makeGrid (CraftBase::CHIP,
                   { M::SLIME, M::TNT, M::MOSS, M::SAND, M::OBSIDIAN, M::CLOUD }),
                   0x22353c26a1aacae6ULL });

    // The 8 spec recipe grids (plain craft result — overrides live in JSON).
    int n = 0;
    const auto* pats = specRecipePatterns (n);
    const std::uint64_t recipeHashes[8] =
    {
        0xde0714a864eeb751ULL,   // PERMAFROST
        0x7ed88c661c14049eULL,   // MAGMA FLOOR
        0x1dc389ffe9f0935dULL,   // QUARRY KICK
        0x062a08dd47760540ULL,   // SHARDSTORM
        0xcaa1201c5e4958afULL,   // FOREST LULLABY
        0xeb40159a1550161eULL,   // MIDAS MODE
        0x2435e7c09fd33f7eULL,   // STRATOSPHERE
        0x01084ca7d2600b88ULL,   // ICICLE HARP
    };
    for (int i = 0; i < n && i < 8; ++i)
        v.push_back ({ pats[i].name, pats[i].grid, recipeHashes[i] });

    return v;
}

static void test_craft_determinism_golden()
{
    std::printf ("[craft_determinism_golden]\n");
    const bool printMode = std::getenv ("BLOCKWAVE_PRINT_CRAFT_HASHES") != nullptr;

    for (const auto& g : goldenGrids())
    {
        const auto h = hashSnapshot (craftApply (g.grid));
        if (printMode)
            std::printf ("  0x%016llxULL  %s\n",
                         static_cast<unsigned long long> (h), g.label);
        // Re-craft must be bit-identical (pure function).
        CHECK_MSG (hashSnapshot (craftApply (g.grid)) == h,
                   "%s: craftApply is not deterministic", g.label);
        if (! printMode)
            CHECK_MSG (h == g.hash,
                       "%s: hash 0x%016llx != golden 0x%016llx (tables changed?)",
                       g.label, static_cast<unsigned long long> (h),
                       static_cast<unsigned long long> (g.hash));
    }
    std::printf ("  %zu golden grids verified%s\n", goldenGrids().size(),
                 printMode ? " (PRINT MODE - goldens not asserted)" : "");
}

// ---------------------------------------------------------------------------
static void test_craft_recipe_matching()
{
    std::printf ("[craft_recipe_matching]\n");
    int n = 0;
    const auto* pats = specRecipePatterns (n);
    CHECK_MSG (n == 8, "expected 8 spec recipe patterns, got %d", n);

    for (int i = 0; i < n; ++i)
    {
        // Exact grid -> its own recipe.
        const auto* hit = matchRecipe (pats[i].grid, pats, n);
        CHECK_MSG (hit == &pats[i], "%s: exact pattern did not match itself",
                   pats[i].name);

        // Wrong base -> no match.
        CraftGrid wrongBase = pats[i].grid;
        wrongBase.base = static_cast<CraftBase> (
            (static_cast<int> (wrongBase.base) + 1) % kNumBases);
        CHECK_MSG (matchRecipe (wrongBase, pats, n) == nullptr,
                   "%s: matched with the wrong base", pats[i].name);

        // Position sensitivity: move one filled cell to an empty cell.
        CraftGrid moved = pats[i].grid;
        int from = -1, to = -1;
        for (int c = 0; c < kNumCells; ++c)
        {
            if (from < 0 && moved.cells[c] != Material::none) from = c;
            if (to < 0 && moved.cells[c] == Material::none)   to = c;
        }
        if (from >= 0 && to >= 0)
        {
            std::swap (moved.cells[from], moved.cells[to]);
            CHECK_MSG (matchRecipe (moved, pats, n) != &pats[i],
                       "%s: matched after moving a cell (not position-sensitive)",
                       pats[i].name);
        }

        // A changed cell (extra material on an empty cell, or a swapped-out
        // material on a full ring) breaks the exact match.
        CraftGrid extra = pats[i].grid;
        bool changed = false;
        for (int c = 0; c < kNumCells && ! changed; ++c)
            if (extra.cells[c] == Material::none)
            {
                extra.cells[c] = Material::SAND;
                changed = true;
            }
        if (! changed)                       // full ring (STRATOSPHERE)
            extra.cells[0] = extra.cells[0] == Material::SAND ? Material::ICE
                                                              : Material::SAND;
        CHECK_MSG (matchRecipe (extra, pats, n) != &pats[i],
                   "%s: matched with a changed cell", pats[i].name);
    }
    std::printf ("  8 patterns: exact hit, wrong-base / moved / extra all miss\n");
}

// ---------------------------------------------------------------------------
static void test_craft_autoname()
{
    std::printf ("[craft_autoname]\n");
    char buf[128];

    const auto name = [&] (const CraftGrid& g)
    { return std::string (autoName (g, buf, sizeof (buf))); };

    CHECK_MSG (name (makeGrid (CraftBase::PAD)) == "Pad",
               "empty grid name: '%s'", buf);
    CHECK_MSG (name (makeGrid (CraftBase::LEAD, { Material::ICE, Material::GOLD }))
                   == "Frozen Golden Lead",       // spec example; tie -> table order
               "ICE+GOLD LEAD name: '%s'", buf);
    CHECK_MSG (name (makeGrid (CraftBase::BASS, { Material::TNT, Material::MOSS }))
                   == "Volatile Mossy Bass",      // spec example
               "TNT+MOSS BASS name: '%s'", buf);
    // Count outranks table order.
    CHECK_MSG (name (makeGrid (CraftBase::CHIP,
                               { Material::CLOUD, Material::CLOUD, Material::ICE }))
                   == "Cloudy Frozen Chip",
               "count-ordering name: '%s'", buf);
    // Cap at 3 adjectives.
    CHECK_MSG (name (makeGrid (CraftBase::KEYS,
                               { Material::ICE, Material::LAVA, Material::WOOD,
                                 Material::SAND })) == "Frozen Molten Wooden Keys",
               "4-material cap name: '%s'", buf);
}

// ---------------------------------------------------------------------------
static void test_craft_dice_mutate()
{
    std::printf ("[craft_dice_mutate]\n");

    // DICE: deterministic per seed, base kept, materials valid.
    {
        CraftGrid a = makeGrid (CraftBase::PERC), b = makeGrid (CraftBase::PERC);
        craftDice (a, 12345);
        craftDice (b, 12345);
        CHECK_MSG (a == b, "dice not deterministic for equal seeds");
        CHECK_MSG (a.base == CraftBase::PERC, "dice changed the base");

        CraftGrid c = makeGrid (CraftBase::PERC);
        craftDice (c, 54321);
        CHECK_MSG (! (a == c), "dice identical across different seeds");

        int filled = 0;
        for (int i = 0; i < kNumCells; ++i)
        {
            const int m = static_cast<int> (a.cells[i]);
            CHECK_MSG (m >= 0 && m <= kNumMaterials, "dice produced invalid material");
            filled += m != 0 ? 1 : 0;
        }
        std::printf ("  dice(12345): %d/8 cells filled\n", filled);
    }

    // MUTATE: deterministic per seed, moves params, stays inside SPEC ranges.
    {
        const auto base = craftApply (makeGrid (CraftBase::PAD, { Material::ICE }));
        ParamSnapshot m1 = base, m2 = base, m3 = base;
        craftMutate (m1, 777);
        craftMutate (m2, 777);
        craftMutate (m3, 778);
        CHECK_MSG (hashSnapshot (m1) == hashSnapshot (m2),
                   "mutate not deterministic for equal seeds");
        CHECK_MSG (hashSnapshot (m1) != hashSnapshot (m3),
                   "mutate identical across different seeds");
        CHECK_MSG (hashSnapshot (m1) != hashSnapshot (base), "mutate was a no-op");

        ParamSnapshot clamped = m1;
        clampSnapshotToSpecRanges (clamped);
        CHECK_MSG (hashSnapshot (clamped) == hashSnapshot (m1),
                   "mutate left values outside SPEC ranges");
    }
}

// ---------------------------------------------------------------------------
// Distinctness matrix (DoD proxy; metric rationale in the file header).
// ---------------------------------------------------------------------------

struct DistinctMetrics { double rmsDelta, centShift, specDist, tailDelta; };

static double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    double acc = 0.0;
    to = std::min (to, x.size());
    if (from >= to) return 0.0;
    for (size_t i = from; i < to; ++i)
        acc += static_cast<double> (x[i]) * x[i];
    return std::sqrt (acc / static_cast<double> (to - from));
}

// Hann-windowed linear magnitude spectrum of n samples starting at 'from'.
static std::vector<double> magSpec (const std::vector<float>& x, size_t from, size_t n)
{
    std::vector<std::complex<double>> buf (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double s = from + i < x.size() ? static_cast<double> (x[from + i]) : 0.0;
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846
                                               * static_cast<double> (i) / static_cast<double> (n));
        buf[i] = std::complex<double> (s * w, 0.0);
    }
    fft (buf);
    std::vector<double> mag (n / 2);
    for (size_t i = 0; i < n / 2; ++i)
        mag[i] = std::abs (buf[i]);
    return mag;
}

static double centroidHz (const std::vector<double>& mag, double sr)
{
    const double binHz = sr / (2.0 * static_cast<double> (mag.size()));
    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < mag.size(); ++i)
    {
        num += static_cast<double> (i) * binHz * mag[i];
        den += mag[i];
    }
    return den > 1.0e-12 ? num / den : 0.0;
}

// Cosine distance between sqrt-compressed spectra (compression keeps strong
// partials from dominating the comparison completely).
static double spectralCosineDist (const std::vector<double>& a, const std::vector<double>& b)
{
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const double x = std::sqrt (a[i]), y = std::sqrt (b[i]);
        dot += x * y;
        na += x * x;
        nb += y * y;
    }
    if (na < 1.0e-12 || nb < 1.0e-12)
        return 1.0;
    return 1.0 - dot / std::sqrt (na * nb);
}

static DistinctMetrics compareRenders (const Rendered& a, const Rendered& b, double sr)
{
    const auto mono = [] (const Rendered& r)
    {
        std::vector<float> m (r.l.size());
        for (size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (r.l[i] + r.r[i]);
        return m;
    };
    const auto ma = mono (a), mb = mono (b);

    DistinctMetrics d {};
    const double rmsA = rmsOf (ma, 0, ma.size());
    const double rmsB = rmsOf (mb, 0, mb.size());
    d.rmsDelta = std::abs (rmsA - rmsB) / std::max ({ rmsA, rmsB, 1.0e-9 });

    const size_t fftStart = static_cast<size_t> (0.02 * sr);
    const size_t fftN = 32768;
    const auto sa = magSpec (ma, fftStart, fftN);
    const auto sb = magSpec (mb, fftStart, fftN);
    const double ca = centroidHz (sa, sr), cb = centroidHz (sb, sr);
    d.centShift = (ca > 1.0 && cb > 1.0) ? std::abs (std::log2 (ca / cb)) : 1.0;
    d.specDist = spectralCosineDist (sa, sb);

    const auto t0 = static_cast<size_t> (1.0 * sr);
    const auto t1 = static_cast<size_t> (1.5 * sr);
    const double tailA = rmsOf (ma, t0, t1), tailB = rmsOf (mb, t0, t1);
    d.tailDelta = std::abs (tailA - tailB) / std::max ({ tailA, tailB, 1.0e-9 });
    return d;
}

static void test_craft_material_distinctness()
{
    std::printf ("[craft_material_distinctness]\n");
    const double sr = 48000.0;

    // Thresholds: a pair is "distinct" if ANY metric clears its bar. Bars sit
    // well below obvious audibility (e.g. a 0.06-octave centroid move or a 6%
    // level change is plainly audible on a bare synth tone) but far above the
    // metrics' noise floor for identical-ish renders (~1e-3).
    const double kRms = 0.06, kCent = 0.06, kSpec = 0.04, kTail = 0.30;

    int worstFails = 0;
    double worstMargin = 1.0e9;
    const char* worstPair = "";

    for (int b = 0; b < kNumBases; ++b)
    {
        const auto base = static_cast<CraftBase> (b);
        const int note = (base == CraftBase::BASS || base == CraftBase::DRONE) ? 36 : 48;
        const auto bare = renderNote (craftApply (makeGrid (base)), note, sr, 0.9, 1.5);

        for (int m = 1; m <= kNumMaterials; ++m)
        {
            CraftGrid g = makeGrid (base);
            g.cells[0] = static_cast<Material> (m);
            const auto r = renderNote (craftApply (g), note, sr, 0.9, 1.5);
            const auto d = compareRenders (bare, r, sr);

            // Margin = best metric relative to its threshold (>1 passes).
            const double margin = std::max ({ d.rmsDelta / kRms, d.centShift / kCent,
                                              d.specDist / kSpec, d.tailDelta / kTail });
            static char pairName[64];
            std::snprintf (pairName, sizeof (pairName), "%s_%s",
                           baseName (base), materialName (static_cast<Material> (m)));
            std::printf ("  %-14s rms=%6.3f cent=%6.3f spec=%6.3f tail=%6.3f  margin=%6.2f%s\n",
                         pairName, d.rmsDelta, d.centShift, d.specDist, d.tailDelta,
                         margin, margin < 1.0 ? "  << FAIL" : "");
            if (margin < worstMargin)
            {
                worstMargin = margin;
                worstPair = materialName (static_cast<Material> (m));
            }
            if (margin < 1.0)
                ++worstFails;
        }
    }
    std::printf ("  worst margin %.2f (%s)\n", worstMargin, worstPair);
    CHECK_MSG (worstFails == 0,
               "%d base/material pairs not distinct from the bare base", worstFails);
}

// ---------------------------------------------------------------------------
inline void runAll()
{
    test_craft_shapeless_and_stacking();
    test_craft_determinism_golden();
    test_craft_recipe_matching();
    test_craft_autoname();
    test_craft_dice_mutate();
    test_craft_material_distinctness();
}

} // namespace crafttests
