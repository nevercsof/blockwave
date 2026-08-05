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
//  - shapeless crafting + diminishing-stack math (exact expected values in
//    the soft-knee's linear region; knee-region values via craftdetail's
//    softDelta, whose own properties get dedicated checks)
//  - soft-knee stacking clamp: railed parameters stay strictly monotonic and
//    strictly inside the SPEC rails for 1..8 copies (architect item, Phase-5
//    follow-up; previously ICE/OBSIDIAN/TNT/SAND/CLOUD stacks hard-clamped)
//  - fixed-order conflict resolution (VOLT/SLIME/MOSS lfo2, STONE raw)
//  - determinism golden hashes (DoD): fixed grids -> frozen snapshot hashes
//  - recipe pattern matching: the 8 frozen spec patterns exist and hit,
//    perturbations miss (iterates whatever specRecipePatterns() returns —
//    growing the pattern table must not require edits here)
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
#include <cstring>
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

// Grid from (material, weight) pairs in cell order 0..7.
struct WeightedCell { Material m; float w; };

static CraftGrid makeWeightedGrid (CraftBase b,
                                   std::initializer_list<WeightedCell> cells)
{
    CraftGrid g;
    g.base = b;
    int i = 0;
    for (const auto& c : cells)
        if (i < kNumCells)
        {
            g.cells[i] = c.m;
            g.setCellWeight (i, c.w);
            ++i;
        }
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

        // mul x2.5: copy 1 full (2.75 s — inside the knee's linear region, so
        // bit-exact); copy 2 at half strength lands the raw 4.8125 s in the
        // knee (start 0.75 * headroom = 1.1 + 0.75*3.9 = 4.025 s) and is
        // compressed by softDelta — the same pure function craftApply uses,
        // whose linear/monotonic/sub-rail properties get their own checks in
        // test_craft_soft_knee_stacking.
        const float r0 = p0.env1_r;
        CHECK_MSG (std::abs (p1.env1_r - r0 * 2.5f) < 1.0e-5f,
                   "ICE x1 release: %.4f != %.4f", (double) p1.env1_r, (double) (r0 * 2.5f));
        const float r2 = craftdetail::softDelta (r0, r0 * 2.5f * 1.75f, 0.0f, 5.0f);
        CHECK_MSG (p2.env1_r == r2,
                   "ICE x2 release: %.4f != soft-kneed %.4f", (double) p2.env1_r,
                   (double) r2);
        CHECK_MSG (p2.env1_r > p1.env1_r && p2.env1_r < 5.0f,
                   "ICE x2 release %.4f not between ICE x1 and the 5 s rail",
                   (double) p2.env1_r);

        // add +2 uni at weights 1.0 / 1.5 on PAD's 5.
        CHECK_MSG (p1.uni_count == 7, "ICE x1 uni_count %d != 7", p1.uni_count);
        CHECK_MSG (p2.uni_count == 8, "ICE x2 uni_count %d != 8 (5+3)", p2.uni_count);

        // set PW -> 38 does not intensify with stacking.
        CHECK_MSG (p1.oscA_pw == 38.0f && p2.oscA_pw == 38.0f,
                   "ICE PW set drifted: %.2f / %.2f",
                   (double) p1.oscA_pw, (double) p2.oscA_pw);

        // SPEC ranges hold under a full stack of 8 (soft knee: floats stay
        // strictly inside the rails, integers round to the rail).
        const auto p8 = craftApply (makeGrid (CraftBase::PAD,
            { Material::ICE, Material::ICE, Material::ICE, Material::ICE,
              Material::ICE, Material::ICE, Material::ICE, Material::ICE }));
        CHECK_MSG (p8.uni_count == 8, "ICE x8 uni_count clamp: %d", p8.uni_count);
        CHECK_MSG (p8.filt_cutoff < 20000.0f && p8.cave_mix < 1.0f && p8.env1_r < 5.0f,
                   "ICE x8 railed a float param (knee should keep it strictly inside)");
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
// Soft-knee stacking clamp. Every case below is a parameter that used to hit
// the hard SPEC clamp within 1-3 copies (the architect's list: ICE x3,
// OBSIDIAN x2, TNT x2, SAND x3, CLOUD x8): with the knee, stacking 1..8
// copies must stay strictly monotonic, strictly inside the rail, and the
// first copies must still move the parameter by a clearly audible amount.
// Plus direct unit checks on craftdetail::softDelta itself, since the exact-
// math test above uses it to compute knee-region expectations.
// ---------------------------------------------------------------------------
static void test_craft_soft_knee_stacking()
{
    std::printf ("[craft_soft_knee_stacking]\n");
    using craftdetail::softDelta;

    // softDelta unit properties (the exact-math test leans on these).
    {
        // Linear region: bit-exact passthrough of the raw value.
        CHECK_MSG (softDelta (1.0f, 3.0f, 0.0f, 5.0f) == 3.0f,
                   "softDelta not identity in the linear region");
        CHECK_MSG (softDelta (1.0f, 0.5f, 0.0f, 5.0f) == 0.5f,
                   "softDelta not identity for small negative deltas");
        CHECK_MSG (softDelta (2.0f, 2.0f, 0.0f, 5.0f) == 2.0f,
                   "softDelta not identity at delta 0");
        // Reference on the rail stays pinned (sets like sustain 0 / PW 38
        // must remain exactly reachable).
        CHECK_MSG (softDelta (0.0f, -1.0f, 0.0f, 1.0f) == 0.0f,
                   "softDelta must pin when the reference sits on the rail");
        CHECK_MSG (softDelta (1.0f, 7.0f, 0.0f, 1.0f) == 1.0f,
                   "softDelta must pin at the hi rail too");
        // Knee region: strictly monotonic, strictly inside the rail, and
        // continuous at the knee start (0.75 * headroom).
        float prev = 0.0f;
        bool monotonic = true, inside = true;
        for (int k = 1; k <= 200; ++k)
        {
            const float raw = 1.0f + 0.1f * static_cast<float> (k);   // 1.1 .. 21
            const float y = softDelta (1.0f, raw, 0.0f, 5.0f);
            if (y <= prev)  monotonic = false;
            if (y >= 5.0f)  inside = false;
            prev = y;
        }
        CHECK_MSG (monotonic, "softDelta not strictly monotonic through the knee");
        CHECK_MSG (inside, "softDelta reached the rail");
        const float kneeStart = 1.0f + 0.75f * 4.0f;                  // = 4
        CHECK_MSG (softDelta (1.0f, kneeStart, 0.0f, 5.0f) == kneeStart,
                   "softDelta must be exact at the knee start");
    }

    // Stacked-material monotonicity on the previously-railed parameters.
    struct RailCase
    {
        const char* label;
        CraftBase base;
        Material mat;
        float ParamSnapshot::* field;
        float rail;              // the rail the stack drives toward
        bool  increasing;        // direction of the stack
        float minEarlyMove;      // |value(3 copies) - value(1 copy)| must exceed
    };
    const RailCase cases[] =
    {
        { "ICE->PAD env1_r (railed 5s at x3)",       CraftBase::PAD,  Material::ICE,
          &ParamSnapshot::env1_r,      5.0f,  true,  0.10f },
        { "OBSIDIAN->BASS sub_level (railed 1)",     CraftBase::BASS, Material::OBSIDIAN,
          &ParamSnapshot::sub_level,   1.0f,  true,  0.004f },
        { "TNT->PERC env2_pitch (railed +48st)",     CraftBase::PERC, Material::TNT,
          &ParamSnapshot::env2_pitch, 48.0f,  true,  0.50f },
        { "SAND->KEYS noise_level (railed 1 at x2)", CraftBase::KEYS, Material::SAND,
          &ParamSnapshot::noise_level, 1.0f,  true,  0.030f },
        { "CLOUD->DRONE cave_size (railed 1 at x2)", CraftBase::DRONE, Material::CLOUD,
          &ParamSnapshot::cave_size,   1.0f,  true,  0.008f },
        { "CLOUD->DRONE cave_mix (0.95 near-rail)",  CraftBase::DRONE, Material::CLOUD,
          &ParamSnapshot::cave_mix,    1.0f,  true,  0.020f },
    };

    for (const auto& c : cases)
    {
        float vals[9] = {};
        std::uint64_t hashes[9] = {};
        for (int n = 0; n <= 8; ++n)
        {
            CraftGrid g;
            g.base = c.base;
            for (int i = 0; i < n; ++i)
                g.cells[i] = c.mat;
            const auto p = craftApply (g);
            vals[n] = p.*(c.field);
            hashes[n] = hashSnapshot (p);
        }
        bool monotonic = true, inside = true, hashesMove = true;
        for (int n = 1; n <= 8; ++n)
        {
            const bool up = vals[n] > vals[n - 1];
            if ((c.increasing && ! up) || (! c.increasing && vals[n] >= vals[n - 1]))
                monotonic = false;
            if (c.increasing ? vals[n] >= c.rail : vals[n] <= c.rail)
                inside = false;
            if (hashes[n] == hashes[n - 1])
                hashesMove = false;
        }
        const float earlyMove = std::abs (vals[3] - vals[1]);
        std::printf ("  %-42s n=1..8: %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                     c.label, (double) vals[1], (double) vals[2], (double) vals[3],
                     (double) vals[4], (double) vals[5], (double) vals[6],
                     (double) vals[7], (double) vals[8]);
        CHECK_MSG (monotonic, "%s: stack not strictly monotonic", c.label);
        CHECK_MSG (inside, "%s: stack reached the rail %.3f", c.label, (double) c.rail);
        CHECK_MSG (earlyMove >= c.minEarlyMove,
                   "%s: copies 1->3 moved only %.4f (need >= %.4f)",
                   c.label, (double) earlyMove, (double) c.minEarlyMove);
        CHECK_MSG (hashesMove, "%s: some copy count was a complete no-op", c.label);
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
    v.push_back ({ "PERC_empty",  makeGrid (CraftBase::PERC),  0xab72198902cd0e45ULL });
    v.push_back ({ "DRONE_empty", makeGrid (CraftBase::DRONE), 0x0139b1033ad668b3ULL });

    // Stacked + mixed grids.
    v.push_back ({ "PAD_ICE",     makeGrid (CraftBase::PAD, { M::ICE }),
                   0xe5c25ae4434a5a1fULL });
    v.push_back ({ "PAD_ICEx4",   makeGrid (CraftBase::PAD,
                   { M::ICE, M::ICE, M::ICE, M::ICE }), 0xd18e88692535ec35ULL });
    v.push_back ({ "LEAD_LAVA_GLASS", makeGrid (CraftBase::LEAD,
                   { M::LAVA, M::GLASS }), 0x63b2470c07d4dab7ULL });
    v.push_back ({ "BASS_mix8",   makeGrid (CraftBase::BASS,
                   { M::ICE, M::LAVA, M::STONE, M::WOOD,
                     M::GLASS, M::GOLD, M::CRYSTAL, M::VOLT }),
                   0xc093c349c958ca6bULL });
    v.push_back ({ "CHIP_mix6",   makeGrid (CraftBase::CHIP,
                   { M::SLIME, M::TNT, M::MOSS, M::SAND, M::OBSIDIAN, M::CLOUD }),
                   0xe8227cf7301235f3ULL });

    // Weighted grids (per-cell WEIGHT/MIX). Frozen the same way as the rest —
    // everything above is untouched, these are new entries only.
    v.push_back ({ "PAD_ICE_w50", makeWeightedGrid (CraftBase::PAD,
                   { { M::ICE, 0.5f } }), 0xd31065d37eab83f6ULL });
    v.push_back ({ "PAD_ICEx4_w_mixed", makeWeightedGrid (CraftBase::PAD,
                   { { M::ICE, 1.0f }, { M::ICE, 0.75f },
                     { M::ICE, 0.5f }, { M::ICE, 0.25f } }),
                   0xfd9703a471b87391ULL });
    v.push_back ({ "BASS_mix8_w25", makeWeightedGrid (CraftBase::BASS,
                   { { M::ICE, 0.25f }, { M::LAVA, 0.25f }, { M::STONE, 0.25f },
                     { M::WOOD, 0.25f }, { M::GLASS, 0.25f }, { M::GOLD, 0.25f },
                     { M::CRYSTAL, 0.25f }, { M::VOLT, 0.25f } }),
                   0xbc0a0e3ca96de7f8ULL });
    v.push_back ({ "CHIP_mix6_w_ramp", makeWeightedGrid (CraftBase::CHIP,
                   { { M::SLIME, 0.1f }, { M::TNT, 0.3f }, { M::MOSS, 0.5f },
                     { M::SAND, 0.7f }, { M::OBSIDIAN, 0.9f }, { M::CLOUD, 1.0f } }),
                   0xc7d3aa5735d2b2abULL });

    // The spec recipe grids (plain craft result — overrides live in JSON).
    // Golden hashes are keyed by NAME, so the pattern table can grow without
    // touching this list; a listed name that vanished from the table fails.
    struct NamedHash { const char* name; std::uint64_t hash; };
    static const NamedHash recipeHashes[] =
    {
        { "PERMAFROST",     0xd6bed827abbc9ef1ULL },
        { "MAGMA FLOOR",    0x08486017213c0bd2ULL },
        { "QUARRY KICK",    0xa78236f16f77b301ULL },
        { "SHARDSTORM",     0x01a0bb9510705123ULL },
        { "FOREST LULLABY", 0x10301394300940bcULL },
        { "MIDAS MODE",     0x97a5c63df67abfcfULL },
        { "STRATOSPHERE",   0x75d591e1c4a1c926ULL },
        { "ICICLE HARP",    0x01084ca7d2600b88ULL },
    };
    int n = 0;
    const auto* pats = specRecipePatterns (n);
    for (const auto& rh : recipeHashes)
    {
        bool found = false;
        for (int i = 0; i < n; ++i)
            if (std::strcmp (pats[i].name, rh.name) == 0)
            {
                v.push_back ({ pats[i].name, pats[i].grid, rh.hash });
                found = true;
                break;
            }
        CHECK_MSG (found, "spec recipe '%s' missing from specRecipePatterns()", rh.name);
    }

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

    // The 8 CRAFT_GRID.md spec recipes are frozen and must exist; the table
    // itself may grow (data-driven book coverage lives in
    // tests/CraftCoverageTests.h) — no exact-count assertion here.
    static const char* const kFrozenSpecNames[] =
    {
        "PERMAFROST", "MAGMA FLOOR", "QUARRY KICK", "SHARDSTORM",
        "FOREST LULLABY", "MIDAS MODE", "STRATOSPHERE", "ICICLE HARP",
    };
    CHECK_MSG (n >= 8, "spec pattern table shrank below the 8 frozen recipes (%d)", n);
    for (const char* name : kFrozenSpecNames)
    {
        bool found = false;
        for (int i = 0; i < n && ! found; ++i)
            found = std::strcmp (pats[i].name, name) == 0;
        CHECK_MSG (found, "frozen spec recipe '%s' missing from the pattern table", name);
    }

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
    std::printf ("  %d patterns: exact hit, wrong-base / moved / extra all miss\n", n);
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
// Per-cell WEIGHT / MIX (src/CraftEngine.h, CELL WEIGHT block). One test per
// rule in that contract, because each rule is load-bearing:
//   1. weights are invisible to recipe identity and auto-naming
//   2. adds/muls scale per copy, defaults are bit-identical to the old engine
//   3. sets are weighted toward their target, exact at 1.0
//   4. discrete switches fire for any non-zero weight
//   5. weight 0 == the material is not there (bit-identical to an empty cell)
//   6. crafting stays SHAPELESS with mixed weights
// ---------------------------------------------------------------------------
static void test_craft_cell_weights()
{
    std::printf ("[craft_cell_weights]\n");

    // ---- default: 1.0 everywhere, and explicitly writing 1.0 changes nothing
    {
        CraftGrid fresh;
        bool allDefault = true;
        for (int i = 0; i < kNumCells; ++i)
            if (fresh.cellWeight (i) != 1.0f)
                allDefault = false;
        CHECK_MSG (allDefault, "a fresh CraftGrid must start at weight 1.0");

        for (const auto& g : goldenGrids())
        {
            CraftGrid explicitOnes = g.grid;
            for (int i = 0; i < kNumCells; ++i)
                explicitOnes.setCellWeight (i, 1.0f);
            if (! g.grid.allCellWeightsDefault())
                continue;                       // weighted golden: not a 1.0 grid
            CHECK_MSG (hashSnapshot (craftApply (explicitOnes))
                           == hashSnapshot (craftApply (g.grid)),
                       "%s: writing weight 1.0 explicitly changed the craft", g.label);
        }
        std::printf ("  default 1.0 is a no-op on every unweighted golden grid\n");
    }

    // ---- rule 5: weight 0 == the material is not there (bit-identical)
    {
        int checked = 0;
        for (const int b : { 0, 1, 2, 4, 7 })
        {
            const auto base = static_cast<CraftBase> (b);
            const auto bare = hashSnapshot (craftApply (makeGrid (base)));
            for (int m = 1; m <= kNumMaterials; ++m)
            {
                const auto zeroed = makeWeightedGrid (base,
                    { { static_cast<Material> (m), 0.0f } });
                CHECK_MSG (hashSnapshot (craftApply (zeroed)) == bare,
                           "%s + %s at weight 0 is not identical to the bare base",
                           baseName (base), materialName (static_cast<Material> (m)));
                ++checked;
            }
        }
        // A zero copy alongside a full one contributes nothing either.
        const auto oneFull = makeWeightedGrid (CraftBase::PAD, { { Material::ICE, 1.0f } });
        const auto fullPlusZero = makeWeightedGrid (CraftBase::PAD,
            { { Material::ICE, 1.0f }, { Material::ICE, 0.0f } });
        CHECK_MSG (hashSnapshot (craftApply (oneFull)) == hashSnapshot (craftApply (fullPlusZero)),
                   "a weight-0 copy alongside a full one changed the result");
        std::printf ("  %d base/material pairs: weight 0 == empty cell, bit-identical\n",
                     checked);
    }

    // ---- rule 6: shapeless — only the multiset of (material, weight) counts
    {
        CraftGrid a = makeWeightedGrid (CraftBase::LEAD,
            { { Material::ICE, 1.0f }, { Material::ICE, 0.5f } });
        CraftGrid b;                            // same pair, swapped and moved
        b.base = CraftBase::LEAD;
        b.cells[6] = Material::ICE; b.setCellWeight (6, 0.5f);
        b.cells[2] = Material::ICE; b.setCellWeight (2, 1.0f);
        CHECK_MSG (snapshotsEqual (craftApply (a), craftApply (b)),
                   "shapeless violated: weight placement changed the craft");

        // ...and the heavier cell is always the primary copy, whichever cell
        // it sits in: 1.0 + 0.5*0.5 = 1.25 total weight either way.
        CraftGrid c;
        c.base = CraftBase::LEAD;
        c.cells[0] = Material::ICE; c.setCellWeight (0, 0.5f);
        c.cells[7] = Material::ICE; c.setCellWeight (7, 1.0f);
        CHECK_MSG (snapshotsEqual (craftApply (a), craftApply (c)),
                   "copy ordering is position dependent — sort by weight broken");
    }

    // ---- rule 2: add/mul arithmetic, exact expected values
    {
        const auto p0 = craftApply (makeGrid (CraftBase::PAD));         // detune 14
        // One copy at 0.5: effective weight 0.5 -> +12 * 0.5 = +6.
        const auto pHalf = craftApply (makeWeightedGrid (CraftBase::PAD,
            { { Material::ICE, 0.5f } }));
        CHECK_MSG (std::abs (pHalf.uni_detune - (p0.uni_detune + 6.0f)) < 1.0e-5f,
                   "ICE@50%% uni_detune %.4f != %.4f", (double) pHalf.uni_detune,
                   (double) (p0.uni_detune + 6.0f));
        // Two copies, 1.0 then 0.5: 1*1.0 + 0.5*0.5 = 1.25 -> +15.
        const auto pMix = craftApply (makeWeightedGrid (CraftBase::PAD,
            { { Material::ICE, 1.0f }, { Material::ICE, 0.5f } }));
        CHECK_MSG (std::abs (pMix.uni_detune - (p0.uni_detune + 15.0f)) < 1.0e-5f,
                   "ICE@100%%+ICE@50%% uni_detune %.4f != %.4f",
                   (double) pMix.uni_detune, (double) (p0.uni_detune + 15.0f));
        // Multiplicative: one ICE copy at w scales cutoff by 1 + 0.25*w.
        const float expected = p0.filt_cutoff * (1.0f + 0.25f * 0.5f);
        CHECK_MSG (std::abs (pHalf.filt_cutoff - expected) < 1.0e-2f,
                   "ICE@50%% cutoff %.3f != %.3f", (double) pHalf.filt_cutoff,
                   (double) expected);
    }

    // ---- rule 3: sets are weighted toward their target, exact at 1.0
    {
        const float padPw = craftApply (makeGrid (CraftBase::PAD)).oscA_pw;   // 50
        struct Case { float w; float expected; };
        const Case cases[] =
        {
            { 1.00f, 38.0f },                              // exact target
            { 0.50f, padPw + 0.5f * (38.0f - padPw) },     // 44
            { 0.25f, padPw + 0.25f * (38.0f - padPw) },    // 47
        };
        for (const auto& c : cases)
        {
            const auto p = craftApply (makeWeightedGrid (CraftBase::PAD,
                { { Material::ICE, c.w } }));
            CHECK_MSG (std::abs (p.oscA_pw - c.expected) < 1.0e-4f,
                       "ICE@%.0f%% PW set %.4f != %.4f", (double) (c.w * 100.0f),
                       (double) p.oscA_pw, (double) c.expected);
        }
        // Full weight must be EXACT, not merely close (bit-identity guard).
        CHECK_MSG (craftApply (makeWeightedGrid (CraftBase::PAD,
                       { { Material::ICE, 1.0f } })).oscA_pw == 38.0f,
                   "a set at weight 1.0 must land exactly on its target");
        // Stacking a set still does not intensify it, and the STRONGEST cell
        // owns it: [0.25, 1.0] must land on the full target.
        CHECK_MSG (craftApply (makeWeightedGrid (CraftBase::PAD,
                       { { Material::ICE, 0.25f }, { Material::ICE, 1.0f } })).oscA_pw
                       == 38.0f,
                   "the strongest copy must own the set");
    }

    // ---- rule 4: discrete switches fire for any non-zero weight
    {
        CHECK_MSG (craftApply (makeWeightedGrid (CraftBase::CHIP,
                       { { Material::STONE, 0.01f } })).raw,
                   "STONE at 1%% must still switch raw ON");
        CHECK_MSG (! craftApply (makeWeightedGrid (CraftBase::CHIP,
                       { { Material::STONE, 0.0f } })).raw,
                   "STONE at 0%% must NOT switch raw ON");
        CHECK_MSG (craftApply (makeWeightedGrid (CraftBase::KEYS,
                       { { Material::CRYSTAL, 0.05f } })).oscB_sync,
                   "CRYSTAL at 5%% must still switch sync ON");
        CHECK_MSG (! craftApply (makeWeightedGrid (CraftBase::KEYS,
                       { { Material::SAND, 0.0f } })).noise_on,
                   "SAND at 0%% must NOT switch noise ON");
        // Synced LFO rates are discrete on purpose (they must stay on the
        // note-division grid); the DEPTH is what the weight scales.
        const auto volt25 = craftApply (makeWeightedGrid (CraftBase::LEAD,
            { { Material::VOLT, 0.25f } }));
        const auto volt100 = craftApply (makeWeightedGrid (CraftBase::LEAD,
            { { Material::VOLT, 1.0f } }));
        CHECK_MSG (volt25.lfo1_rate == volt100.lfo1_rate && volt25.lfo1_rate == 0.0625f,
                   "VOLT lfo1_rate must stay on the 1/16 division at any weight");
        CHECK_MSG (volt25.lfo1_pwm < volt100.lfo1_pwm,
                   "VOLT lfo1_pwm depth must scale with weight (%.4f vs %.4f)",
                   (double) volt25.lfo1_pwm, (double) volt100.lfo1_pwm);
    }

    // ---- rule 1: weights are invisible to recipe identity and auto-naming
    {
        int n = 0;
        const auto* pats = specRecipePatterns (n);
        for (int i = 0; i < n; ++i)
        {
            for (const float w : { 0.0f, 0.3f, 0.65f, 1.0f })
            {
                CraftGrid g = pats[i].grid;
                for (int c = 0; c < kNumCells; ++c)
                    g.setCellWeight (c, w);
                CHECK_MSG (matchRecipe (g, pats, n) == &pats[i],
                           "%s stopped matching at weight %.2f — weights must "
                           "never touch recipe detection", pats[i].name, (double) w);
                CHECK_MSG (g == pats[i].grid,
                           "%s: operator== must ignore weights", pats[i].name);
                CHECK_MSG (! g.equalsWithWeights (pats[i].grid) || w == 1.0f,
                           "%s: equalsWithWeights must NOT ignore weights", pats[i].name);
            }
        }
        char buf[128];
        CraftGrid named = makeWeightedGrid (CraftBase::LEAD,
            { { Material::ICE, 0.0f }, { Material::GOLD, 0.1f } });
        CHECK_MSG (std::string (autoName (named, buf, sizeof (buf))) == "Frozen Golden Lead",
                   "auto-name must describe what is PLACED, ignoring weights: '%s'", buf);
        std::printf ("  %d spec recipes match at weights 0 / 0.3 / 0.65 / 1.0\n", n);
    }

    // ---- monotonicity: turning a material up must keep moving the sound
    {
        struct MonoCase
        {
            const char* label;
            CraftBase base;
            Material mat;
            float ParamSnapshot::* field;
            bool increasing;
        };
        const MonoCase cases[] =
        {
            { "PAD+ICE cave_mix",        CraftBase::PAD,  Material::ICE,
              &ParamSnapshot::cave_mix,   true  },
            { "PAD+ICE uni_detune",      CraftBase::PAD,  Material::ICE,
              &ParamSnapshot::uni_detune, true  },
            { "BASS+OBSIDIAN cutoff",    CraftBase::BASS, Material::OBSIDIAN,
              &ParamSnapshot::filt_cutoff, false },
            { "KEYS+CLOUD env1_a",       CraftBase::KEYS, Material::CLOUD,
              &ParamSnapshot::env1_a,     true  },
            { "LEAD+LAVA filt_res",      CraftBase::LEAD, Material::LAVA,
              &ParamSnapshot::filt_res,   true  },
            { "CHIP+MOSS crush_mix",     CraftBase::CHIP, Material::MOSS,
              &ParamSnapshot::crush_mix,  true  },
        };
        constexpr float kWeights[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (const auto& c : cases)
        {
            float v[5] = {};
            for (int i = 0; i < 5; ++i)
                v[i] = craftApply (makeWeightedGrid (c.base, { { c.mat, kWeights[i] } }))
                           .*(c.field);
            bool mono = true;
            for (int i = 1; i < 5; ++i)
                if (c.increasing ? v[i] <= v[i - 1] : v[i] >= v[i - 1])
                    mono = false;
            std::printf ("  %-24s 0%%/25%%/50%%/75%%/100%%: %.4f %.4f %.4f %.4f %.4f\n",
                         c.label, (double) v[0], (double) v[1], (double) v[2],
                         (double) v[3], (double) v[4]);
            CHECK_MSG (mono, "%s: not strictly monotonic across weights", c.label);
        }
    }
}

// ---------------------------------------------------------------------------
// The weight slider must be AUDIBLE, and monotonically so, in the RENDER —
// not just in the parameter set. Each pair is scored on the metric that
// carries its material's character, because a generic "distance from the 0%
// render" is not a monotone function of anything: detuned-unison beating and
// integer steps (unison count, octave) make it wander.
//   PAD + ICE       -> +2 unison, +detune, PW 50->38%  => the body thins out
//   BASS + OBSIDIAN -> cutoff down, octave down    => the centroid falls
//   KEYS + CLOUD    -> +300 ms attack, level down  => the first 150 ms quieten
//   LEAD + GLASS    -> cutoff x1.4, PW 18% -> 14%  => the centroid rises
// All three metrics are printed for every pair so a human can audit the ones
// the case does not assert on.
static void test_craft_weight_audible_monotonic()
{
    std::printf ("[craft_weight_audible_monotonic]\n");
    const double sr = 48000.0;
    constexpr float kWeights[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    enum class Metric { tailRms, centroid, earlyRms };
    struct Case
    {
        CraftBase base; Material mat; int note;
        Metric metric; bool increasing; const char* what;
        double minTotalMove;      // |100% - 0%| relative to the 0% value
    };
    const Case cases[] =
    {
        { CraftBase::PAD,  Material::ICE,      48,
          Metric::earlyRms, false, "early rms", 0.30 },
        { CraftBase::BASS, Material::OBSIDIAN, 36,
          Metric::centroid, false, "centroid", 0.30 },
        { CraftBase::KEYS, Material::CLOUD,    48,
          Metric::earlyRms, false, "early rms", 0.30 },
        { CraftBase::LEAD, Material::GLASS,    48,
          Metric::centroid, true,  "centroid", 0.15 },
    };

    for (const auto& c : cases)
    {
        double tail[5] = {}, cent[5] = {}, early[5] = {};
        for (int i = 0; i < 5; ++i)
        {
            const auto r = renderNote (craftApply (makeWeightedGrid (c.base,
                                           { { c.mat, kWeights[i] } })),
                                       c.note, sr, 0.9, 1.5);
            std::vector<float> mono (r.l.size());
            for (size_t k = 0; k < mono.size(); ++k)
                mono[k] = 0.5f * (r.l[k] + r.r[k]);
            tail[i]  = rmsOf (mono, static_cast<size_t> (1.0 * sr),
                                    static_cast<size_t> (1.5 * sr));
            early[i] = rmsOf (mono, 0, static_cast<size_t> (0.15 * sr));
            cent[i]  = centroidHz (magSpec (mono, static_cast<size_t> (0.02 * sr), 32768), sr);
        }
        const double* v = c.metric == Metric::tailRms  ? tail
                        : c.metric == Metric::centroid ? cent
                                                       : early;

        std::printf ("  %-5s + %-9s 0/25/50/75/100%%\n", baseName (c.base),
                     materialName (c.mat));
        std::printf ("      tail rms  %.5f %.5f %.5f %.5f %.5f\n",
                     tail[0], tail[1], tail[2], tail[3], tail[4]);
        std::printf ("      early rms %.5f %.5f %.5f %.5f %.5f\n",
                     early[0], early[1], early[2], early[3], early[4]);
        std::printf ("      centroid  %.0f %.0f %.0f %.0f %.0f Hz  <- asserting on %s\n",
                     cent[0], cent[1], cent[2], cent[3], cent[4], c.what);

        for (int i = 1; i < 5; ++i)
            CHECK_MSG (c.increasing ? v[i] > v[i - 1] : v[i] < v[i - 1],
                       "%s + %s: %s not monotonic from weight %.0f%% to %.0f%% "
                       "(%.5f -> %.5f)", baseName (c.base), materialName (c.mat),
                       c.what, (double) (kWeights[i - 1] * 100.0f),
                       (double) (kWeights[i] * 100.0f), v[i - 1], v[i]);
        const double move = std::abs (v[4] - v[0]) / std::max (std::abs (v[0]), 1.0e-9);
        CHECK_MSG (move >= c.minTotalMove,
                   "%s + %s: the whole slider only moved %s by %.1f%% (need %.0f%%)",
                   baseName (c.base), materialName (c.mat), c.what,
                   move * 100.0, c.minTotalMove * 100.0);
    }
}

// ---------------------------------------------------------------------------
inline void runAll()
{
    test_craft_shapeless_and_stacking();
    test_craft_soft_knee_stacking();
    test_craft_cell_weights();
    test_craft_weight_audible_monotonic();
    test_craft_determinism_golden();
    test_craft_recipe_matching();
    test_craft_autoname();
    test_craft_dice_mutate();
    test_craft_material_distinctness();
}

} // namespace crafttests
