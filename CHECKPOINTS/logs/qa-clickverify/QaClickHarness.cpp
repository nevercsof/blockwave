// QA independent click harness — written by qa-runner, NOT derived from
// tests/CraftCoverageTests.h. Measures the two Phase-4 regression transitions
// through the RECIPE-AWARE path (craftSnapshotWithRecipes) and, as a control,
// through the raw craftApply path.
//
// Two independent detectors:
//   SLEW  = max |x[i]-x[i-1]| inside the swap window, normalised by the
//           steady-state slew on either side of the swap.
//   PEAK  = max |x| inside the swap window, normalised by steady-state peak.
//           A filter flung open by octaves shows up here as a level burst even
//           if the per-sample slew were somehow tame.

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>

#include "PluginProcessor.h"
#include "CraftJson.h"
#include "BlockwaveEngine.h"

using namespace blockwave;

namespace
{
constexpr double kSwapWinSec   = 0.030;  // window we consider "the transition"
constexpr double kPreWinSec    = 0.150;  // steady reference before swap
constexpr double kPostSkipSec  = 0.200;  // skip glide, then steady reference
constexpr double kTailSec      = 0.500;

struct Render { std::vector<float> l, r; };

// Render one held note, swapping A->B at an exact sample index.
Render renderSwap (const ParamSnapshot& a, const ParamSnapshot& b,
                   int note, float vel, double sr, int block,
                   double swapSec, int& swapSampleOut)
{
    const int total  = static_cast<int> ((swapSec + kTailSec) * sr);
    const int stepAt = static_cast<int> (swapSec * sr);
    swapSampleOut = stepAt;

    BlockwaveEngine e;
    e.setParams (a);
    e.setTempo (120.0);
    e.prepare (sr, block);
    e.noteOn (note, vel);

    Render out;
    out.l.assign (static_cast<size_t> (total), 0.0f);
    out.r.assign (static_cast<size_t> (total), 0.0f);

    bool swapped = false;
    for (int pos = 0; pos < total; )
    {
        if (! swapped && pos >= stepAt) { e.setParams (b); swapped = true; }
        int n = std::min (block, total - pos);
        if (! swapped && pos + n > stepAt)
            n = stepAt - pos;                 // land the swap on an exact sample
        e.process (out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

float maxSlew (const std::vector<float>& x, int from, int to)
{
    float m = 0.0f;
    const int lo = std::max (1, from);
    const int hi = std::min (static_cast<int> (x.size()), to);
    for (int i = lo; i < hi; ++i)
        m = std::max (m, std::fabs (x[static_cast<size_t> (i)]
                                  - x[static_cast<size_t> (i - 1)]));
    return m;
}

float maxAbs (const std::vector<float>& x, int from, int to)
{
    float m = 0.0f;
    const int lo = std::max (0, from);
    const int hi = std::min (static_cast<int> (x.size()), to);
    for (int i = lo; i < hi; ++i)
        m = std::max (m, std::fabs (x[static_cast<size_t> (i)]));
    return m;
}

struct Score { float slew = 0.0f, peak = 0.0f; double at = 0.0;
               float absSwap = 0.0f, absBound = 0.0f; };

// Worst case over a set of swap instants (the click is phase dependent).
Score score (const ParamSnapshot& a, const ParamSnapshot& b,
             int note, float vel, double sr, int block)
{
    // Deliberately NOT the same instants as the repo test, so we are not
    // measuring a set of instants the fix could have been tuned to.
    const double instants[] = { 0.37, 0.55, 0.63, 0.81, 0.94, 1.12, 1.35, 1.66 };
    Score worst;
    for (const double t : instants)
    {
        int stepAt = 0;
        const auto rr = renderSwap (a, b, note, vel, sr, block, t, stepAt);
        const int win = static_cast<int> (kSwapWinSec * sr);

        const float sSwap = maxSlew (rr.l, stepAt, stepAt + win);
        const float sPre  = maxSlew (rr.l, stepAt - static_cast<int> (kPreWinSec * sr), stepAt);
        const float sPost = maxSlew (rr.l, stepAt + static_cast<int> (kPostSkipSec * sr),
                                     static_cast<int> (rr.l.size()));

        const float pSwap = maxAbs (rr.l, stepAt, stepAt + win);
        const float pPre  = maxAbs (rr.l, stepAt - static_cast<int> (kPreWinSec * sr), stepAt);
        const float pPost = maxAbs (rr.l, stepAt + static_cast<int> (kPostSkipSec * sr),
                                    static_cast<int> (rr.l.size()));

        const float slewR = sSwap / std::max (1.0e-9f, 1.25f * std::max (sPre, sPost));
        const float peakR = pSwap / std::max (1.0e-9f, 1.25f * std::max (pPre, pPost));

        if (slewR > worst.slew)
        {
            worst.slew = slewR; worst.at = t;
            worst.absSwap = sSwap; worst.absBound = 1.25f * std::max (sPre, sPost);
        }
        worst.peak = std::max (worst.peak, peakR);
    }
    return worst;
}

CraftGrid grid (CraftBase b, Material fill = Material::none, int count = 0)
{
    CraftGrid g;
    g.base = b;
    for (int i = 0; i < count && i < kNumCells; ++i)
        g.cells[i] = fill;
    return g;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File json = juce::File (BLOCKWAVE_TEST_DIR).getParentDirectory()
                                .getChildFile ("data").getChildFile ("recipes.json");
    if (! json.existsAsFile())
    {
        std::printf ("FATAL: %s missing\n", json.getFullPathName().toRawUTF8());
        return 2;
    }

    RecipeBook book;
    juce::String err;
    if (! book.loadFromJson (json.loadFileAsString(), err))
    {
        std::printf ("FATAL: recipe book parse failed: %s\n", err.toRawUTF8());
        return 2;
    }
    std::printf ("recipe book: %d recipes\n\n", book.getNumRecipes());

    const CraftGrid padObs8  = grid (CraftBase::PAD, Material::OBSIDIAN, 8);  // SINKHOLE
    const CraftGrid bass     = grid (CraftBase::BASS);
    const CraftGrid bassLava = grid (CraftBase::BASS, Material::LAVA, 1);
    const CraftGrid padIce   = grid (CraftBase::PAD, Material::ICE, 1);
    const CraftGrid pad      = grid (CraftBase::PAD);

    struct Case { const char* label; CraftGrid from, to; };
    const Case cases[] =
    {
        { "REGRESSION  PAD+OBSIDIANx8 -> BASS",      padObs8, bass     },
        { "REGRESSION  PAD+OBSIDIANx8 -> BASS+LAVA", padObs8, bassLava },
        { "control     PAD -> PAD+ICE",              pad,     padIce   },
        { "control     PAD -> BASS+LAVA",            pad,     bassLava },
    };

    const int note = 60;
    const float vel = 100.0f / 127.0f;

    const std::pair<double,int> configs[] =
    {
        { 48000.0, 512 }, { 48000.0, 64 }, { 44100.0, 512 }, { 44100.0, 64 },
        { 44100.0, 16 }, { 48000.0, 4096 }, { 96000.0, 128 }, { 192000.0, 1024 },
    };

    std::printf ("=== RECIPE-AWARE path (craftSnapshotWithRecipes) ===\n");
    std::printf ("%-42s %10s %8s %8s\n", "case", "sr/block", "slewR", "peakR");
    float worstRegSlew = 0.0f, worstCtlSlew = 0.0f;
    for (const auto& c : cases)
    {
        const auto a = craftSnapshotWithRecipes (c.from, &book);
        const auto b = craftSnapshotWithRecipes (c.to,   &book);
        for (const auto& cf : configs)
        {
            const auto s = score (a, b, note, vel, cf.first, cf.second);
            std::printf ("%-42s %6.0f/%-4d %8.2f %8.2f  step %.5f bound %.5f\n", c.label,
                         cf.first, cf.second,
                         static_cast<double> (s.slew), static_cast<double> (s.peak),
                         static_cast<double> (s.absSwap), static_cast<double> (s.absBound));
            if (std::string (c.label).rfind ("REGRESSION", 0) == 0)
                worstRegSlew = std::max (worstRegSlew, s.slew);
            else
                worstCtlSlew = std::max (worstCtlSlew, s.slew);
        }
    }

    std::printf ("\n=== RAW path (craftApply, no recipe overrides) — expected to hide the defect ===\n");
    for (int i = 0; i < 2; ++i)
    {
        const auto& c = cases[i];
        const auto a = craftApply (c.from);
        const auto b = craftApply (c.to);
        const auto s = score (a, b, note, vel, 48000.0, 512);
        std::printf ("%-42s %6.0f/%-4d %8.2f %8.2f  step %.5f bound %.5f\n", c.label, 48000.0, 512,
                     static_cast<double> (s.slew), static_cast<double> (s.peak),
                     static_cast<double> (s.absSwap), static_cast<double> (s.absBound));
    }

    std::printf ("\nWORST regression slewR = %.2f   WORST control slewR = %.2f\n",
                 static_cast<double> (worstRegSlew), static_cast<double> (worstCtlSlew));
    return 0;
}
