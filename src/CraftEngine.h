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

// CRAFT engine (Phase 4, docs/CRAFT_GRID.md). Pure C++ — no JUCE, no I/O, no
// allocation. Everything here is a deterministic function of its inputs so
// the same grid produces the bit-identical ParamSnapshot on every machine
// (only IEEE basic arithmetic is used: + - * / and lround — no pow/exp/log,
// which differ between platform math libraries).
//
// Threading: message thread (or any non-realtime context). The audio thread
// never calls into this file; crafted snapshots reach it through the same
// atomic APVTS parameter path as every other edit.
//
// CELL INDEXING (frozen, used by presets, recipes and the UI):
//   the 8 outer cells of the 3x3 grid in reading order, skipping the center:
//        0 1 2
//        3 . 4      (center '.' = BASE)
//        5 6 7
//
// MECHANICS (CRAFT_GRID.md):
//  - center = one of 8 hand-tuned BASE archetype patches;
//  - materials are shapeless; the k-th copy of the same material (k = 0,1,..)
//    carries weight 2^-k (1.0, 0.5, 0.25, ...);
//  - every placed cell also carries its own WEIGHT / MIX in 0..1 (default 1.0
//    = 100%). A cell's effective contribution is copyWeight(k) * cellWeight,
//    with the copies of a material sorted by DESCENDING cell weight so the
//    strongest cell is always copy 0 — that is what keeps crafting shapeless
//    (only the multiset of (material, weight) pairs matters, never where the
//    blocks sit). See the CELL WEIGHT block below for the full contract;
//  - deltas apply in fixed material table order (ICE..CLOUD):
//      add     : value += delta * (sum of copy weights)
//      mul     : per copy, value *= 1 + (factor - 1) * copyWeight
//                (copy 1 = the full x-factor; extras diminish; only basic
//                 arithmetic, so results are cross-platform deterministic)
//      set     : value = target (a placed material always has weight >= 1,
//                so "PW -> 38%" style targets land exactly; stacking a set
//                does not intensify it)
//      switch  : discrete ON at weight >= 1; switches only ever turn things
//                ON, so the fixed order resolves all conflicts; STONE's raw
//                is applied after the material loop ("wins last")
//  - the accumulated delta on every continuous parameter is soft-kneed
//    against the frozen SPEC rails (craftdetail::softDelta): linear up to
//    75% of the headroom, then compressed asymptotically, so stacked copies
//    keep changing the sound instead of slamming into the range; integer
//    parameters round and hard-clamp as before.

#include <cmath>
#include <cstdint>
#include <cstring>
#include "BlockwaveParams.h"

namespace blockwave
{

// ---------------------------------------------------------------------------
// Grid data types
// ---------------------------------------------------------------------------

enum class CraftBase : int
{
    LEAD = 0, BASS, PAD, PLUCK, KEYS, CHIP, PERC, DRONE
};
constexpr int kNumBases = 8;

// Table order (CRAFT_GRID.md §Materials) — the delta application order.
enum class Material : int
{
    none = 0,
    ICE, LAVA, STONE, WOOD, GLASS, GOLD, CRYSTAL,
    VOLT, SLIME, TNT, MOSS, SAND, OBSIDIAN, CLOUD
};
constexpr int kNumMaterials = 14;
constexpr int kNumCells     = 8;

// ---------------------------------------------------------------------------
// CELL WEIGHT / MIX (v1.0, producer-specified — architect had deferred it to
// v1.1; the producer overrode that and fixed the semantics below).
//
// Every placed cell carries a weight in 0..1 (1.0 = 100%, the default) that
// scales that cell's contribution to its material's delta set. The rules, in
// full, because every one of them is load-bearing:
//
//  1. RECIPE DETECTION IGNORES WEIGHTS, ALWAYS. A recipe matches on base +
//     material identity + position only, so a grid with the right blocks at
//     30% (or 0%) still triggers the recipe. This protects every already
//     discovered recipe and the whole hunt mechanic, and it is why
//     CraftGrid::operator== — the one function matchRecipe/RecipeBook::match
//     use — deliberately does NOT look at weights. Use equalsWithWeights()
//     when you need full equality (state/preset round-trip checks).
//     Auto-naming ignores weights for the same reason: name and recipe both
//     describe what you PLACED; weights only shape how it sounds.
//
//  2. ADDS / MULTIPLIES scale per copy: copy k contributes
//     copyWeight(k) * cellWeight(k), copies sorted by descending cell weight.
//     With every weight at 1.0 the arithmetic is bit-identical to the
//     pre-weight engine (multiplying by exactly 1.0f is exact in IEEE), so
//     the frozen craft golden hashes and every factory preset are unaffected.
//
//  3. SETS are weighted toward their target by the material's STRONGEST cell
//     weight: value += wMax * (target - value), and exactly `value = target`
//     at wMax == 1. Stacking still does not intensify a set. Weighting sets
//     is what makes "turn this material down" actually turn it down: a
//     material whose whole character is a set (GLASS's PW -> 14%) would
//     otherwise ignore the slider completely.
//
//  4. DISCRETE SWITCHES (raw, sync, noise on, LFO destinations/shapes) fire
//     for ANY non-zero weight. A bool cannot be half on; weight 0 is the only
//     value that removes them. The tempo-synced LFO RATES (lfo1_rate,
//     lfo2_rate) count as discrete here on purpose: interpolating them would
//     park a synced LFO between note divisions. Turning a material down
//     reduces the modulation DEPTH (lfo1_pwm / lfo2_amt are adds, so they
//     scale) while the wobble stays in time.
//
//  5. WEIGHT 0 == THE MATERIAL IS NOT THERE, for the delta math only. A
//     material whose cells are all at 0 is skipped outright, so the result is
//     bit-identical to leaving those cells empty — while the blocks stay
//     PLACED for rule 1.
//
//  6. The soft-knee stacking clamp is unchanged and runs on top: effective
//     contributions are summed exactly as before, then knee-mapped.
constexpr float kCellWeightDefault = 1.0f;

inline float clampCellWeight (float w) noexcept
{
    return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
}

struct CraftGrid
{
    CraftBase base = CraftBase::LEAD;
    Material  cells[kNumCells] = {};    // all Material::none by default
    // Per-cell weight / mix, 0..1. Meaningful only where cells[i] != none.
    float weights[kNumCells] = { kCellWeightDefault, kCellWeightDefault,
                                 kCellWeightDefault, kCellWeightDefault,
                                 kCellWeightDefault, kCellWeightDefault,
                                 kCellWeightDefault, kCellWeightDefault };

    // RECIPE IDENTITY: base + placement only (rule 1 above). Weights are
    // deliberately excluded — do not "fix" this.
    bool operator== (const CraftGrid& o) const noexcept
    {
        if (base != o.base)
            return false;
        for (int i = 0; i < kNumCells; ++i)
            if (cells[i] != o.cells[i])
                return false;
        return true;
    }

    // Full equality including the weights of PLACED cells (an empty cell's
    // weight is meaningless and is never compared, saved or restored).
    bool equalsWithWeights (const CraftGrid& o) const noexcept
    {
        if (! (*this == o))
            return false;
        for (int i = 0; i < kNumCells; ++i)
            if (cells[i] != Material::none && weights[i] != o.weights[i])
                return false;
        return true;
    }

    float cellWeight (int i) const noexcept
    {
        return i >= 0 && i < kNumCells ? clampCellWeight (weights[i])
                                       : kCellWeightDefault;
    }

    void setCellWeight (int i, float w) noexcept
    {
        if (i >= 0 && i < kNumCells)
            weights[i] = clampCellWeight (w);
    }

    // True when every placed cell sits at the 1.0 default — the case where
    // the "weights" JSON key is omitted entirely (see CraftJson.h).
    bool allCellWeightsDefault() const noexcept
    {
        for (int i = 0; i < kNumCells; ++i)
            if (cells[i] != Material::none && weights[i] != kCellWeightDefault)
                return false;
        return true;
    }
};

// Canonical ALL-CAPS token names (preset/recipe JSON representation).
inline const char* baseName (CraftBase b) noexcept
{
    static const char* names[kNumBases] =
        { "LEAD", "BASS", "PAD", "PLUCK", "KEYS", "CHIP", "PERC", "DRONE" };
    return names[static_cast<int> (b)];
}

inline const char* materialName (Material m) noexcept
{
    static const char* names[kNumMaterials + 1] =
        { "", "ICE", "LAVA", "STONE", "WOOD", "GLASS", "GOLD", "CRYSTAL",
          "VOLT", "SLIME", "TNT", "MOSS", "SAND", "OBSIDIAN", "CLOUD" };
    return names[static_cast<int> (m)];
}

inline bool baseFromName (const char* s, CraftBase& out) noexcept
{
    for (int i = 0; i < kNumBases; ++i)
        if (std::strcmp (s, baseName (static_cast<CraftBase> (i))) == 0)
        {
            out = static_cast<CraftBase> (i);
            return true;
        }
    return false;
}

inline bool materialFromName (const char* s, Material& out) noexcept
{
    if (s[0] == '\0') { out = Material::none; return true; }
    for (int i = 1; i <= kNumMaterials; ++i)
        if (std::strcmp (s, materialName (static_cast<Material> (i))) == 0)
        {
            out = static_cast<Material> (i);
            return true;
        }
    return false;
}

// ---------------------------------------------------------------------------
// Auto-naming (CRAFT_GRID.md #5): material adjectives + base noun.
// Adjectives ordered by descending material weight (= copy count), ties by
// table order, capped at 3. "Frozen Golden Lead", "Volatile Mossy Bass".
// ---------------------------------------------------------------------------

inline const char* baseNoun (CraftBase b) noexcept
{
    static const char* nouns[kNumBases] =
        { "Lead", "Bass", "Pad", "Pluck", "Keys", "Chip", "Perc", "Drone" };
    return nouns[static_cast<int> (b)];
}

inline const char* materialAdjective (Material m) noexcept
{
    // Documented for the UI tooltips / naming (index = Material enum):
    static const char* adj[kNumMaterials + 1] =
        { "",
          "Frozen",    // ICE
          "Molten",    // LAVA
          "Stony",     // STONE
          "Wooden",    // WOOD
          "Glassy",    // GLASS
          "Golden",    // GOLD
          "Crystal",   // CRYSTAL
          "Electric",  // VOLT
          "Slimy",     // SLIME
          "Volatile",  // TNT
          "Mossy",     // MOSS
          "Sandy",     // SAND
          "Obsidian",  // OBSIDIAN
          "Cloudy" };  // CLOUD
    return adj[static_cast<int> (m)];
}

// Writes the generated name into out (NUL-terminated, cap outSize incl. NUL).
// Empty grid -> just the base noun ("Pad"). Returns out.
inline const char* autoName (const CraftGrid& g, char* out, int outSize) noexcept
{
    int counts[kNumMaterials + 1] = {};
    for (int i = 0; i < kNumCells; ++i)
        ++counts[static_cast<int> (g.cells[i])];
    counts[0] = 0;

    // Up to 3 adjectives: highest copy count first, ties by table order.
    int picked[3];
    int numPicked = 0;
    for (int slot = 0; slot < 3; ++slot)
    {
        int best = 0, bestCount = 0;
        for (int m = 1; m <= kNumMaterials; ++m)
            if (counts[m] > bestCount)
            {
                best = m;
                bestCount = counts[m];
            }
        if (best == 0)
            break;
        picked[numPicked++] = best;
        counts[best] = 0;
    }

    int pos = 0;
    const auto append = [&] (const char* s)
    {
        for (int i = 0; s[i] != '\0' && pos < outSize - 1; ++i)
            out[pos++] = s[i];
    };
    for (int i = 0; i < numPicked; ++i)
    {
        append (materialAdjective (static_cast<Material> (picked[i])));
        append (" ");
    }
    append (baseNoun (g.base));
    out[pos] = '\0';
    return out;
}

// ---------------------------------------------------------------------------
// SPEC range clamp (frozen ranges from docs/SPEC.md; duplicated here so the
// craft path stays JUCE-free — guarded against drift by the state tests).
// ---------------------------------------------------------------------------

inline float craftClamp (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int craftClampRound (float v, int lo, int hi) noexcept
{
    const long r = std::lround (v);
    return r < lo ? lo : (r > hi ? hi : static_cast<int> (r));
}

inline void clampSnapshotToSpecRanges (ParamSnapshot& p) noexcept
{
    p.oscA_oct   = p.oscA_oct < -2 ? -2 : (p.oscA_oct > 2 ? 2 : p.oscA_oct);
    p.oscB_oct   = p.oscB_oct < -2 ? -2 : (p.oscB_oct > 2 ? 2 : p.oscB_oct);
    p.oscA_semi  = p.oscA_semi < -12 ? -12 : (p.oscA_semi > 12 ? 12 : p.oscA_semi);
    p.oscB_semi  = p.oscB_semi < -12 ? -12 : (p.oscB_semi > 12 ? 12 : p.oscB_semi);
    p.oscA_fine  = craftClamp (p.oscA_fine, -100.0f, 100.0f);
    p.oscB_fine  = craftClamp (p.oscB_fine, -100.0f, 100.0f);
    p.oscA_pw    = craftClamp (p.oscA_pw, 1.0f, 99.0f);
    p.oscB_pw    = craftClamp (p.oscB_pw, 1.0f, 99.0f);
    p.oscA_level = craftClamp (p.oscA_level, 0.0f, 1.0f);
    p.oscB_level = craftClamp (p.oscB_level, 0.0f, 1.0f);
    p.sub_level  = craftClamp (p.sub_level, 0.0f, 1.0f);
    p.noise_level = craftClamp (p.noise_level, 0.0f, 1.0f);
    p.uni_count  = p.uni_count < 1 ? 1 : (p.uni_count > 8 ? 8 : p.uni_count);
    p.uni_detune = craftClamp (p.uni_detune, 0.0f, 100.0f);
    p.uni_spread = craftClamp (p.uni_spread, 0.0f, 1.0f);
    p.poly_count = p.poly_count < 1 ? 1 : (p.poly_count > 16 ? 16 : p.poly_count);
    p.glide_time = craftClamp (p.glide_time, 0.0f, 2.0f);
    p.filt_cutoff = craftClamp (p.filt_cutoff, 20.0f, 20000.0f);
    p.filt_res    = craftClamp (p.filt_res, 0.0f, 1.0f);
    p.filt_env    = craftClamp (p.filt_env, -1.0f, 1.0f);
    p.filt_keytrack = craftClamp (p.filt_keytrack, 0.0f, 1.0f);
    p.env1_a = craftClamp (p.env1_a, 0.0f, 5.0f);
    p.env1_d = craftClamp (p.env1_d, 0.0f, 5.0f);
    p.env1_s = craftClamp (p.env1_s, 0.0f, 1.0f);
    p.env1_r = craftClamp (p.env1_r, 0.0f, 5.0f);
    p.env2_a = craftClamp (p.env2_a, 0.0f, 5.0f);
    p.env2_d = craftClamp (p.env2_d, 0.0f, 5.0f);
    p.env2_s = craftClamp (p.env2_s, 0.0f, 1.0f);
    p.env2_r = craftClamp (p.env2_r, 0.0f, 5.0f);
    p.env2_pitch = craftClamp (p.env2_pitch, -48.0f, 48.0f);
    p.lfo1_rate  = craftClamp (p.lfo1_rate, 0.01f, 40.0f);
    p.lfo1_pwm   = craftClamp (p.lfo1_pwm, 0.0f, 1.0f);
    p.lfo2_rate  = craftClamp (p.lfo2_rate, 0.01f, 40.0f);
    p.lfo2_amt   = craftClamp (p.lfo2_amt, -1.0f, 1.0f);
    p.crush_bits = p.crush_bits < 1 ? 1 : (p.crush_bits > 16 ? 16 : p.crush_bits);
    p.crush_down = p.crush_down < 1 ? 1 : (p.crush_down > 64 ? 64 : p.crush_down);
    p.crush_mix  = craftClamp (p.crush_mix, 0.0f, 1.0f);
    p.dly_time   = p.dly_time < 0 ? 0 : (p.dly_time > 10 ? 10 : p.dly_time);
    p.dly_fb     = craftClamp (p.dly_fb, 0.0f, 0.9f);
    p.dly_mix    = craftClamp (p.dly_mix, 0.0f, 1.0f);
    p.cave_size  = craftClamp (p.cave_size, 0.0f, 1.0f);
    p.cave_damp  = craftClamp (p.cave_damp, 0.0f, 1.0f);
    p.cave_mix   = craftClamp (p.cave_mix, 0.0f, 1.0f);
    p.vel_amp    = craftClamp (p.vel_amp, 0.0f, 1.0f);
    p.master_gain = craftClamp (p.master_gain, -60.0f, 6.0f);
}

// ---------------------------------------------------------------------------
// Base archetypes — 8 hand-tuned starting patches (SOUND_DESIGN techniques
// cited inline). Values chosen so every base sounds good completely bare.
// ---------------------------------------------------------------------------

inline ParamSnapshot baseSnapshot (CraftBase b) noexcept
{
    ParamSnapshot p;                      // SPEC defaults
    switch (b)
    {
        case CraftBase::LEAD:             // technique 2: thin pulse lead
            p.oscA_pw    = 18.0f;
            p.uni_count  = 2;
            p.uni_detune = 10.0f;
            p.uni_spread = 0.4f;
            p.filt_cutoff = 9000.0f;
            p.env1_a = 0.002f; p.env1_d = 0.25f; p.env1_s = 0.75f; p.env1_r = 0.15f;
            p.vel_amp = 0.4f;
            break;

        case CraftBase::BASS:             // technique 6: sub-anchored bass
            p.oscA_pw   = 35.0f;
            p.sub_on    = true;
            p.sub_level = 0.8f;
            p.filt_cutoff = 650.0f;
            p.filt_res  = 0.15f;
            p.filt_env  = 0.25f;          // small ENV2 bite on the attack
            p.env2_d    = 0.12f;
            p.env1_a = 0.001f; p.env1_d = 0.18f; p.env1_s = 0.6f; p.env1_r = 0.09f;
            break;

        case CraftBase::PAD:              // technique 1: the money PWM pad
            p.oscA_pw   = 50.0f;
            p.lfo1_pwm  = 0.45f;
            p.lfo1_rate = 2.0f;           // 2/1 synced — slow shimmer
            p.uni_count = 5;
            p.uni_detune = 14.0f;
            p.uni_spread = 0.9f;
            p.filt_cutoff = 4500.0f;
            p.env1_a = 0.45f; p.env1_d = 0.5f; p.env1_s = 0.85f; p.env1_r = 1.1f;
            p.cave_mix = 0.18f;
            p.cave_size = 0.6f;
            p.vel_amp = 0.25f;
            break;

        case CraftBase::PLUCK:            // filter pluck; materials add pitch env
            p.oscA_pw = 30.0f;
            p.filt_cutoff = 1100.0f;
            p.filt_res = 0.18f;
            p.filt_env = 0.55f;
            p.env2_d = 0.14f; p.env2_r = 0.14f;
            p.env1_a = 0.001f; p.env1_d = 0.38f; p.env1_s = 0.0f; p.env1_r = 0.28f;
            p.vel_amp = 0.6f;
            break;

        case CraftBase::KEYS:             // technique 3: hollow 50% keys
            p.oscA_pw = 50.0f;
            p.filt_cutoff = 2600.0f;
            p.env1_a = 0.002f; p.env1_d = 0.55f; p.env1_s = 0.35f; p.env1_r = 0.22f;
            p.vel_amp = 0.75f;
            break;

        case CraftBase::CHIP:             // 25% NES pulse, instant envelope
            p.oscA_pw = 25.0f;
            p.filt_cutoff = 20000.0f;
            p.env1_a = 0.001f; p.env1_d = 0.03f; p.env1_s = 0.9f; p.env1_r = 0.035f;
            p.vel_amp = 0.2f;
            break;

        case CraftBase::PERC:             // technique 8: chip kick
            p.oscA_level = 1.0f;
            p.env2_pitch = 30.0f;         // positive = start high, fall to pitch
                                          // (the falling boom; see SOUND_DESIGN #8)
            p.env2_d = 0.09f; p.env2_s = 0.0f; p.env2_r = 0.05f;
            p.filt_cutoff = 380.0f;
            p.filt_env = 0.2f;
            p.env1_a = 0.001f; p.env1_d = 0.22f; p.env1_s = 0.0f; p.env1_r = 0.08f;
            p.vel_amp = 0.4f;
            break;

        case CraftBase::DRONE:            // technique 13: cave ambience, held
            p.oscA_pw = 50.0f;
            p.sub_on = true;
            p.sub_level = 0.6f;
            p.uni_count = 3;
            p.uni_detune = 8.0f;
            p.uni_spread = 0.6f;
            p.lfo1_pwm = 0.3f;
            p.lfo1_rate = 4.0f;           // 4/1 synced — glacial PWM drift
            p.filt_cutoff = 900.0f;
            p.env1_a = 0.9f; p.env1_d = 0.5f; p.env1_s = 1.0f; p.env1_r = 1.6f;
            p.cave_mix = 0.25f;
            p.cave_size = 0.75f;
            p.vel_amp = 0.1f;
            break;
    }
    return p;
}

// ---------------------------------------------------------------------------
// The craft function
// ---------------------------------------------------------------------------

namespace craftdetail
{
    // The cells holding one material, as their weights sorted DESCENDING —
    // so copy 0 (the full 2^0 copy weight) is always the strongest cell and
    // crafting stays shapeless. Built once per craft by collectCopies().
    struct MaterialCopies
    {
        int   n = 0;
        float w[kNumCells] = {};

        float maxWeight() const noexcept { return n > 0 ? w[0] : 0.0f; }
    };

    inline void collectCopies (const CraftGrid& g,
                               MaterialCopies (&out)[kNumMaterials + 1]) noexcept
    {
        for (int i = 0; i < kNumCells; ++i)
        {
            const int m = static_cast<int> (g.cells[i]);
            if (m <= 0 || m > kNumMaterials)
                continue;
            auto& mc = out[m];
            const float w = g.cellWeight (i);
            int k = mc.n;                          // insertion sort, descending
            while (k > 0 && mc.w[k - 1] < w)
            {
                mc.w[k] = mc.w[k - 1];
                --k;
            }
            mc.w[k] = w;
            ++mc.n;
        }
    }

    // Sum of effective copy weights: copy k contributes 2^-k * cellWeight(k).
    // With every cell at 1.0 this is the classic 2 - 2^(1-n) computed by the
    // same plain summation as before, bit-for-bit (x * 1.0f == x exactly).
    inline float totalWeight (const MaterialCopies& mc) noexcept
    {
        float w = 0.0f, cw = 1.0f;
        for (int k = 0; k < mc.n; ++k)
        {
            w += cw * mc.w[k];
            cw *= 0.5f;
        }
        return w;
    }

    // Multiplicative stacking: copy k scales by 1 + (factor-1) * 2^-k * weight.
    // Same bit-identity argument as totalWeight at weight 1.0.
    inline void mulW (float& x, float factor, const MaterialCopies& mc) noexcept
    {
        float cw = 1.0f;
        for (int k = 0; k < mc.n; ++k)
        {
            x *= 1.0f + (factor - 1.0f) * cw * mc.w[k];
            cw *= 0.5f;
        }
    }

    inline void addW (float& x, float delta, float w) noexcept { x += delta * w; }

    // Weighted 'set' (rule 3): exactly the target at full weight — the
    // explicit branch guarantees bit-identity rather than relying on
    // x + 1.0f * (target - x) rounding back to target.
    inline void setW (float& x, float target, float wMax) noexcept
    {
        if (wMax >= 1.0f)
            x = target;
        else
            x += wMax * (target - x);
    }

    // ---- Soft-knee stacking clamp (CRAFT_GRID.md §Stacking clamp) ----------
    //
    // Material stacking used to hard-clamp at the SPEC rails, so copies 3-8
    // of ICE/OBSIDIAN/TNT/SAND/CLOUD were inaudible no-ops. Instead, the
    // accumulated delta on each continuous parameter is now compressed as it
    // approaches the rail:
    //
    //   refV : the parameter after base archetype + all 'set' operations
    //          (deltas suppressed) — this value must stay exactly reachable,
    //          including values sitting on a rail (sustain 0, cutoff 20k).
    //   rawV : the parameter after the full delta accumulation, unclamped.
    //   d = rawV - refV, headroom h = distance from refV to the rail d aims at.
    //
    //   |d| <= 0.75*h : identity (returns rawV bit-exactly, so hand-tuned
    //                   single-copy deltas keep their documented values)
    //   |d| >  0.75*h : the excess maps through x/(x+c), C1-continuous at the
    //                   knee, asymptotic to h — monotonic in d, strictly
    //                   inside the rail, so every extra copy still moves the
    //                   parameter. Only + - * / — cross-platform exact.
    constexpr float kStackKneeStart = 0.75f;   // knee begins at 75% of headroom

    inline float softDelta (float refV, float rawV, float lo, float hi) noexcept
    {
        const float d = rawV - refV;
        if (d == 0.0f)
            return rawV;
        const float h = d > 0.0f ? hi - refV : refV - lo;
        if (h <= 0.0f)
            return refV;                       // ref on the rail: pinned (old behavior)
        const float a   = d < 0.0f ? -d : d;
        const float lin = kStackKneeStart * h;
        if (a <= lin)
            return rawV;                       // linear region: bit-exact passthrough
        const float cap    = h - lin;
        const float excess = a - lin;
        const float soft   = lin + cap * excess / (excess + cap);
        return d > 0.0f ? refV + soft : refV - soft;
    }

    // Snapshot plus the integer fields materials touch, accumulated as floats
    // and rounded once at the end (shared by the ref and raw passes).
    struct CraftAccum
    {
        ParamSnapshot p;
        float fUniCount  = 1.0f;
        float fCrushBits = 16.0f;
        float fCrushDown = 1.0f;
        float fOscAOct   = 0.0f;
        float fOscBOct   = 0.0f;
        float fOscBSemi  = 0.0f;
    };

    // The material table, applied in fixed order. Runs twice per craft:
    // withDeltas == false -> sets/switches only (the soft-knee reference);
    // withDeltas == true  -> the full accumulation (unclamped raw values).
    // Keeping one body for both passes is what guarantees they agree on the
    // set/switch state and only differ by the add/mul deltas.
    inline void applyMaterials (CraftAccum& A,
                                const MaterialCopies (&copies)[kNumMaterials + 1],
                                bool withDeltas) noexcept
    {
        ParamSnapshot& p = A.p;
        const auto add = [withDeltas] (float& x, float delta, float w) noexcept
        {
            if (withDeltas)
                addW (x, delta, w);
        };
        const auto mul = [withDeltas] (float& x, float factor,
                                       const MaterialCopies& mc) noexcept
        {
            if (withDeltas)
                mulW (x, factor, mc);
        };

        for (int m = 1; m <= kNumMaterials; ++m)
        {
            const MaterialCopies& n = copies[m];
            // Not placed at all, or every copy at weight 0 (rule 5) — either
            // way the material contributes nothing, bit-for-bit.
            if (n.n == 0 || n.w[0] <= 0.0f)
                continue;
            const float w  = totalWeight (n);   // adds: summed effective weight
            const float wm = n.maxWeight();     // sets/switches: strongest cell
            const auto set = [&] (float& x, float target) noexcept
            {
                setW (x, target, wm);
            };

            switch (static_cast<Material> (m))
            {
                case Material::ICE:           // cold, wide, long
                    add (A.fUniCount, 2.0f, w);
                    add (p.uni_detune, 12.0f, w);
                    mul (p.env1_r, 2.5f, n);
                    mul (p.filt_cutoff, 1.25f, n);
                    add (p.cave_mix, 0.15f, w);
                    set (p.oscA_pw, 38.0f);
                    set (p.oscB_pw, 38.0f);
                    break;

                case Material::LAVA:          // hot, aggressive
                    add (p.crush_mix, 0.3f, w);
                    add (A.fCrushBits, -6.0f, w);
                    add (p.filt_res, 0.2f, w);
                    add (p.filt_env, 0.4f, w);
                    p.sub_on = true;          // tuning: +0.2 sub is inaudible with
                    add (p.sub_level, 0.2f, w);    // the sub off; switch it ON
                    break;

                case Material::STONE:         // dry, blunt, raw (raw set after loop)
                    mul (p.env1_r, 0.4f, n);
                    set (p.cave_mix, 0.0f);
                    set (p.oscA_pw, 50.0f);
                    set (p.oscB_pw, 50.0f);
                    mul (p.filt_cutoff, 0.85f, n);
                    break;

                case Material::WOOD:          // warm, mellow
                    mul (p.filt_cutoff, 0.65f, n);
                    set (p.oscA_pw, 47.0f);
                    set (p.oscB_pw, 47.0f);
                    add (p.env1_a, 0.008f, w);
                    add (p.vel_amp, 0.2f, w);
                    break;

                case Material::GLASS:         // thin, bright, delicate
                    set (p.oscA_pw, 14.0f);
                    set (p.oscB_pw, 14.0f);
                    mul (p.filt_cutoff, 1.4f, n);
                    add (p.dly_mix, 0.2f, w);
                    add (p.oscA_level, -0.1f, w);
                    add (p.oscB_level, -0.1f, w);
                    set (p.env1_a, 0.001f);
                    break;

                case Material::GOLD:          // expensive, wide, polished
                    add (A.fUniCount, 3.0f, w);
                    add (p.uni_spread, 0.3f, w);
                    add (p.cave_mix, 0.1f, w);
                    add (p.oscA_fine, 4.0f, w);    // fine +-4c: A up, B down
                    add (p.oscB_fine, -4.0f, w);
                    break;

                case Material::CRYSTAL:       // metallic, singing (technique 5)
                    p.oscB_on = true;         // sync is inaudible with B off
                    p.oscB_sync = true;
                    add (A.fOscBOct, 1.0f, w);     // sync partial up an octave
                    add (A.fOscBSemi, 7.0f, w);
                    add (p.env2_pitch, 5.0f, w);
                    set (p.env2_d, 0.08f);    // fast decay
                    mul (p.filt_cutoff, 1.3f, n);
                    break;

                case Material::VOLT:          // electric, jittery motion
                    add (p.lfo1_pwm, 0.5f, w);
                    p.lfo1_rate = 0.0625f;    // 1/16 synced
                    p.lfo1_sync = true;
                    p.lfo2_dest = Lfo2Dest::cutoff;
                    p.lfo2_shape = LfoShape::sampleHold;
                    p.lfo2_rate = 0.0625f;    // tuning: default 1/4 is too slow
                    p.lfo2_sync = true;       // for audible jitter
                    add (p.lfo2_amt, 0.3f, w);
                    break;

                case Material::SLIME:         // wobbly, gluey
                    add (p.glide_time, 0.12f, w);
                    p.lfo2_dest = Lfo2Dest::pw;
                    p.lfo2_shape = LfoShape::tri;  // tuning: wobble, not steps
                    p.lfo2_rate = 0.125f;     // 1/8 synced
                    p.lfo2_sync = true;
                    add (p.lfo2_amt, 0.4f, w);
                    set (p.oscA_pw, 60.0f);
                    set (p.oscB_pw, 60.0f);
                    break;

                case Material::TNT:           // percussive boom
                    add (p.env2_pitch, 24.0f, w);  // positive = falling drop
                    set (p.env2_d, 0.09f);         // (SOUND_DESIGN technique 8)
                    set (p.env2_s, 0.0f);
                    set (p.env1_s, 0.0f);     // "sustain 0" read as amp sustain:
                    set (p.env1_a, 0.002f);   // every base turns percussive
                    add (p.crush_mix, 0.2f, w);
                    p.noise_on = true;        // noise burst (gated by the amp env)
                    add (p.noise_level, 0.25f, w);
                    break;

                case Material::MOSS:          // lo-fi, chill (technique 11)
                    add (A.fCrushDown, 8.0f, w);
                    add (p.crush_mix, 0.25f, w);
                    mul (p.filt_cutoff, 0.75f, n);
                    p.lfo2_dest = Lfo2Dest::pitch;
                    p.lfo2_shape = LfoShape::tri;
                    p.lfo2_rate = 1.0f;       // 1/1 synced — slow wobble
                    p.lfo2_sync = true;
                    set (p.lfo2_amt, 0.065f); // quadratic taper: ~ +-5 cents
                    break;

                case Material::SAND:          // gritty texture
                    p.noise_on = true;
                    add (p.noise_level, 0.35f, w);
                    p.noise_mode = NoiseMode::longMode;
                    add (p.filt_res, 0.1f, w);
                    break;

                case Material::OBSIDIAN:      // dark, heavy, deep
                    mul (p.filt_cutoff, 0.45f, n);
                    p.sub_on = true;
                    add (p.sub_level, 0.3f, w);
                    add (A.fOscAOct, -1.0f, w);    // "oct -1 tendency", both oscs
                    add (A.fOscBOct, -1.0f, w);
                    mul (p.env1_r, 1.5f, n);
                    break;

                case Material::CLOUD:         // soft, airy, distant
                    add (p.env1_a, 0.3f, w);
                    add (p.cave_mix, 0.35f, w);
                    add (p.cave_size, 0.3f, w);
                    add (p.oscA_level, -0.15f, w);
                    add (p.oscB_level, -0.15f, w);
                    mul (p.filt_cutoff, 0.9f, n);
                    break;

                case Material::none:
                default:
                    break;
            }
        }

        // STONE's raw wins last (conflict rule). A discrete switch, so it
        // follows rule 4: any non-zero STONE weight turns it on.
        if (copies[static_cast<int> (Material::STONE)].maxWeight() > 0.0f)
            p.raw = true;
    }
}

// craft(base, cells) -> full parameter set. Deterministic; see file header.
// Two passes over the material table (sets-only reference + full raw), then
// every continuous parameter goes through the soft-knee stacking clamp.
inline ParamSnapshot craftApply (const CraftGrid& g) noexcept
{
    using namespace craftdetail;

    MaterialCopies copies[kNumMaterials + 1] = {};
    collectCopies (g, copies);

    CraftAccum ref;
    ref.p          = baseSnapshot (g.base);
    ref.fUniCount  = static_cast<float> (ref.p.uni_count);
    ref.fCrushBits = static_cast<float> (ref.p.crush_bits);
    ref.fCrushDown = static_cast<float> (ref.p.crush_down);
    ref.fOscAOct   = static_cast<float> (ref.p.oscA_oct);
    ref.fOscBOct   = static_cast<float> (ref.p.oscB_oct);
    ref.fOscBSemi  = static_cast<float> (ref.p.oscB_semi);
    CraftAccum raw = ref;

    applyMaterials (ref, copies, false);   // sets/switches only
    applyMaterials (raw, copies, true);    // full accumulation, unclamped

    // Bools, enums and 'set' floats come straight from the reference pass
    // (identical in the raw pass); every continuous field is knee-mapped.
    // Field list mirrors clampSnapshotToSpecRanges — same frozen SPEC ranges.
    ParamSnapshot p = ref.p;
    const auto knee = [&] (float ParamSnapshot::* f, float lo, float hi) noexcept
    {
        p.*f = softDelta (ref.p.*f, raw.p.*f, lo, hi);
    };
    knee (&ParamSnapshot::oscA_fine, -100.0f, 100.0f);
    knee (&ParamSnapshot::oscB_fine, -100.0f, 100.0f);
    knee (&ParamSnapshot::oscA_pw, 1.0f, 99.0f);
    knee (&ParamSnapshot::oscB_pw, 1.0f, 99.0f);
    knee (&ParamSnapshot::oscA_level, 0.0f, 1.0f);
    knee (&ParamSnapshot::oscB_level, 0.0f, 1.0f);
    knee (&ParamSnapshot::sub_level, 0.0f, 1.0f);
    knee (&ParamSnapshot::noise_level, 0.0f, 1.0f);
    knee (&ParamSnapshot::uni_detune, 0.0f, 100.0f);
    knee (&ParamSnapshot::uni_spread, 0.0f, 1.0f);
    knee (&ParamSnapshot::glide_time, 0.0f, 2.0f);
    knee (&ParamSnapshot::filt_cutoff, 20.0f, 20000.0f);
    knee (&ParamSnapshot::filt_res, 0.0f, 1.0f);
    knee (&ParamSnapshot::filt_env, -1.0f, 1.0f);
    knee (&ParamSnapshot::filt_keytrack, 0.0f, 1.0f);
    knee (&ParamSnapshot::env1_a, 0.0f, 5.0f);
    knee (&ParamSnapshot::env1_d, 0.0f, 5.0f);
    knee (&ParamSnapshot::env1_s, 0.0f, 1.0f);
    knee (&ParamSnapshot::env1_r, 0.0f, 5.0f);
    knee (&ParamSnapshot::env2_a, 0.0f, 5.0f);
    knee (&ParamSnapshot::env2_d, 0.0f, 5.0f);
    knee (&ParamSnapshot::env2_s, 0.0f, 1.0f);
    knee (&ParamSnapshot::env2_r, 0.0f, 5.0f);
    knee (&ParamSnapshot::env2_pitch, -48.0f, 48.0f);
    knee (&ParamSnapshot::lfo1_rate, 0.01f, 40.0f);
    knee (&ParamSnapshot::lfo1_pwm, 0.0f, 1.0f);
    knee (&ParamSnapshot::lfo2_rate, 0.01f, 40.0f);
    knee (&ParamSnapshot::lfo2_amt, -1.0f, 1.0f);
    knee (&ParamSnapshot::vel_amp, 0.0f, 1.0f);
    knee (&ParamSnapshot::master_gain, -60.0f, 6.0f);
    knee (&ParamSnapshot::crush_mix, 0.0f, 1.0f);
    knee (&ParamSnapshot::dly_fb, 0.0f, 0.9f);
    knee (&ParamSnapshot::dly_mix, 0.0f, 1.0f);
    knee (&ParamSnapshot::cave_size, 0.0f, 1.0f);
    knee (&ParamSnapshot::cave_damp, 0.0f, 1.0f);
    knee (&ParamSnapshot::cave_mix, 0.0f, 1.0f);

    // Integer fields: knee in the float domain, then round and hard-clamp —
    // steps are inherent to integers, a sub-step knee would be inaudible.
    p.uni_count  = craftClampRound (softDelta (ref.fUniCount,  raw.fUniCount,  1.0f, 8.0f), 1, 8);
    p.crush_bits = craftClampRound (softDelta (ref.fCrushBits, raw.fCrushBits, 1.0f, 16.0f), 1, 16);
    p.crush_down = craftClampRound (softDelta (ref.fCrushDown, raw.fCrushDown, 1.0f, 64.0f), 1, 64);
    p.oscA_oct   = craftClampRound (softDelta (ref.fOscAOct,   raw.fOscAOct,  -2.0f, 2.0f), -2, 2);
    p.oscB_oct   = craftClampRound (softDelta (ref.fOscBOct,   raw.fOscBOct,  -2.0f, 2.0f), -2, 2);
    p.oscB_semi  = craftClampRound (softDelta (ref.fOscBSemi,  raw.fOscBSemi, -12.0f, 12.0f), -12, 12);

    clampSnapshotToSpecRanges (p);         // safety net; a no-op after the knee
    return p;
}

// ---------------------------------------------------------------------------
// Recipe patterns (position-sensitive exact match). The 8 spec recipes live
// here as the pure single source of truth; the JSON recipe book (BinaryData)
// carries the same patterns plus the hand-tuned override patches, and the
// state tests assert the two stay in sync.
// ---------------------------------------------------------------------------

struct RecipePattern
{
    const char* name;
    CraftGrid grid;
};

inline const RecipePattern* specRecipePatterns (int& count) noexcept
{
    using M = Material;
    constexpr M o = M::none;
    // Cell order: 0 1 2 / 3 . 4 / 5 6 7 (see file header).
    static const RecipePattern table[] =
    {
        // PAD + ICE across the top row.
        { "PERMAFROST",     { CraftBase::PAD,
            { M::ICE, M::ICE, M::ICE, o, o, o, o, o } } },
        // BASS + OBSIDIAN left+right, LAVA below.
        { "MAGMA FLOOR",    { CraftBase::BASS,
            { o, o, o, M::OBSIDIAN, M::OBSIDIAN, o, M::LAVA, o } } },
        // PERC + TNT top+bottom, SAND left.
        { "QUARRY KICK",    { CraftBase::PERC,
            { o, M::TNT, o, M::SAND, o, o, M::TNT, o } } },
        // LEAD + CRYSTAL in all 4 corners.
        { "SHARDSTORM",     { CraftBase::LEAD,
            { M::CRYSTAL, o, M::CRYSTAL, o, o, M::CRYSTAL, o, M::CRYSTAL } } },
        // KEYS + WOOD, MOSS, CLOUD in a column (left column, top to bottom).
        { "FOREST LULLABY", { CraftBase::KEYS,
            { M::WOOD, o, o, M::MOSS, o, M::CLOUD, o, o } } },
        // CHIP + GOLD in all 4 edge-centers.
        { "MIDAS MODE",     { CraftBase::CHIP,
            { o, M::GOLD, o, M::GOLD, M::GOLD, o, M::GOLD, o } } },
        // DRONE + CLOUD full ring.
        { "STRATOSPHERE",   { CraftBase::DRONE,
            { M::CLOUD, M::CLOUD, M::CLOUD, M::CLOUD,
              M::CLOUD, M::CLOUD, M::CLOUD, M::CLOUD } } },
        // PLUCK + GLASS on the grid diagonal (3 diagonal cells: GLASS,
        // base, GLASS — the base occupies the diagonal's center).
        { "ICICLE HARP",    { CraftBase::PLUCK,
            { M::GLASS, o, o, o, o, o, o, M::GLASS } } },
    };
    count = static_cast<int> (sizeof (table) / sizeof (table[0]));
    return table;
}

// Exact position-sensitive match against a pattern list. nullptr = no match.
inline const RecipePattern* matchRecipe (const CraftGrid& g,
                                         const RecipePattern* patterns,
                                         int count) noexcept
{
    for (int i = 0; i < count; ++i)
        if (patterns[i].grid == g)
            return &patterns[i];
    return nullptr;
}

// ---------------------------------------------------------------------------
// DICE / MUTATE — deterministic under a caller-supplied seed (xorshift64*).
// ---------------------------------------------------------------------------

struct CraftRng
{
    std::uint64_t s;
    explicit CraftRng (std::uint64_t seed) noexcept : s (seed != 0 ? seed : 0x9e3779b97f4a7c15ULL) {}

    std::uint64_t next() noexcept
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545f4914f6cdd1dULL;
    }

    // Uniform in [0, n).
    std::uint32_t nextInt (std::uint32_t n) noexcept
    {
        return static_cast<std::uint32_t> (next() % n);
    }

    // Uniform in [-1, 1].
    float nextBipolar() noexcept
    {
        return (static_cast<float> (next() >> 40) / 8388607.5f) - 1.0f;
    }
};

// DICE: fill the outer cells with random materials, base kept. Each cell has
// a 40% chance of staying empty so results read as recipes, not walls.
// Cell WEIGHTS are deliberately left alone: DICE is specified as "fill random
// cells with random materials (base kept)", and silently resetting the user's
// weight sliders would be a second, undocumented effect.
inline void craftDice (CraftGrid& g, std::uint64_t seed) noexcept
{
    CraftRng rng (seed);
    for (int i = 0; i < kNumCells; ++i)
    {
        if (rng.nextInt (100) < 40)
            g.cells[i] = Material::none;
        else
            g.cells[i] = static_cast<Material> (1 + static_cast<int> (rng.nextInt (kNumMaterials)));
    }
}

// MUTATE: small random offsets on top of the current params (escape hatch to
// non-grid territory). Continuous audible parameters only; everything is
// clamped back to SPEC ranges. Multiplicative jitter uses 1 + r*depth (basic
// arithmetic only) so results stay deterministic per seed.
inline void craftMutate (ParamSnapshot& p, std::uint64_t seed) noexcept
{
    CraftRng rng (seed);
    const auto jog  = [&] (float& x, float span)  { x += rng.nextBipolar() * span; };
    const auto jogM = [&] (float& x, float depth) { x *= 1.0f + rng.nextBipolar() * depth; };

    jog  (p.oscA_pw, 8.0f);
    jog  (p.oscB_pw, 8.0f);
    jogM (p.filt_cutoff, 0.35f);
    jog  (p.filt_res, 0.08f);
    jog  (p.uni_detune, 8.0f);
    jog  (p.uni_spread, 0.15f);
    jogM (p.env1_a, 0.4f);
    jogM (p.env1_d, 0.4f);
    jog  (p.env1_s, 0.1f);
    jogM (p.env1_r, 0.4f);
    jogM (p.env2_d, 0.3f);
    jog  (p.lfo1_pwm, 0.1f);
    jog  (p.lfo2_amt, 0.1f);
    jog  (p.crush_mix, 0.08f);
    jog  (p.dly_mix, 0.08f);
    jog  (p.cave_mix, 0.08f);
    jog  (p.cave_size, 0.1f);
    clampSnapshotToSpecRanges (p);
}

// ---------------------------------------------------------------------------
// Determinism hash — FNV-1a 64 over the canonical field sequence (bools as
// one byte, ints as 4 LE bytes, floats as their IEEE bit pattern, 4 LE
// bytes). Field order = ParamSnapshot declaration order. Used by the golden
// determinism tests; identical on every platform because the craft math is.
// ---------------------------------------------------------------------------

namespace craftdetail
{
    inline void hashBytes (std::uint64_t& h, const void* data, int n) noexcept
    {
        const auto* b = static_cast<const unsigned char*> (data);
        for (int i = 0; i < n; ++i)
        {
            h ^= b[i];
            h *= 0x100000001b3ULL;
        }
    }
    inline void hashBool  (std::uint64_t& h, bool v) noexcept
    {
        const unsigned char b = v ? 1 : 0;
        hashBytes (h, &b, 1);
    }
    inline void hashInt   (std::uint64_t& h, int v) noexcept
    {
        unsigned char b[4];
        const auto u = static_cast<std::uint32_t> (v);
        b[0] = static_cast<unsigned char> (u);
        b[1] = static_cast<unsigned char> (u >> 8);
        b[2] = static_cast<unsigned char> (u >> 16);
        b[3] = static_cast<unsigned char> (u >> 24);
        hashBytes (h, b, 4);
    }
    inline void hashFloat (std::uint64_t& h, float v) noexcept
    {
        std::uint32_t u;
        std::memcpy (&u, &v, 4);
        hashInt (h, static_cast<int> (u));
    }
}

inline std::uint64_t hashSnapshot (const ParamSnapshot& p) noexcept
{
    using namespace craftdetail;
    std::uint64_t h = 0xcbf29ce484222325ULL;
    hashBool (h, p.oscA_on);   hashBool (h, p.oscB_on);
    hashBool (h, p.sub_on);    hashBool (h, p.noise_on);
    hashInt (h, p.oscA_oct);   hashInt (h, p.oscB_oct);
    hashInt (h, p.oscA_semi);  hashInt (h, p.oscB_semi);
    hashFloat (h, p.oscA_fine); hashFloat (h, p.oscB_fine);
    hashFloat (h, p.oscA_pw);   hashFloat (h, p.oscB_pw);
    hashFloat (h, p.oscA_level); hashFloat (h, p.oscB_level);
    hashBool (h, p.oscB_sync);
    hashInt (h, p.sub_oct);    hashFloat (h, p.sub_level);
    hashInt (h, static_cast<int> (p.noise_mode));
    hashFloat (h, p.noise_level);
    hashInt (h, p.uni_count);
    hashFloat (h, p.uni_detune); hashFloat (h, p.uni_spread);
    hashInt (h, static_cast<int> (p.voice_mode));
    hashInt (h, p.poly_count);
    hashFloat (h, p.glide_time);
    hashInt (h, static_cast<int> (p.glide_mode));
    hashInt (h, static_cast<int> (p.filt_type));
    hashFloat (h, p.filt_cutoff); hashFloat (h, p.filt_res);
    hashFloat (h, p.filt_env);    hashFloat (h, p.filt_keytrack);
    hashFloat (h, p.env1_a); hashFloat (h, p.env1_d);
    hashFloat (h, p.env1_s); hashFloat (h, p.env1_r);
    hashFloat (h, p.env2_a); hashFloat (h, p.env2_d);
    hashFloat (h, p.env2_s); hashFloat (h, p.env2_r);
    hashFloat (h, p.env2_pitch);
    hashFloat (h, p.lfo1_rate); hashBool (h, p.lfo1_sync);
    hashFloat (h, p.lfo1_pwm);
    hashFloat (h, p.lfo2_rate); hashBool (h, p.lfo2_sync);
    hashInt (h, static_cast<int> (p.lfo2_shape));
    hashFloat (h, p.lfo2_amt);
    hashInt (h, static_cast<int> (p.lfo2_dest));
    hashFloat (h, p.vel_amp);
    hashBool (h, p.raw);
    hashFloat (h, p.master_gain);
    hashInt (h, p.crush_bits); hashInt (h, p.crush_down);
    hashFloat (h, p.crush_mix);
    hashInt (h, p.dly_time);   hashFloat (h, p.dly_fb);
    hashBool (h, p.dly_pingpong);
    hashFloat (h, p.dly_mix);
    hashFloat (h, p.cave_size); hashFloat (h, p.cave_damp);
    hashFloat (h, p.cave_mix);
    return h;
}

} // namespace blockwave
