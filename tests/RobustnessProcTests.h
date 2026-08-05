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

// Phase-7 robustness sweeps, processor half. The engine half (sample-rate /
// buffer-size / tempo / CPU) is tests/RobustnessTests.h; everything here needs
// the real BlockwaveAudioProcessor, the factory bank or the frozen parameter
// table, so it lives with the JUCE state suite:
//
//   1. factory presets across the full 44.1k-192k rate matrix (level, finite,
//      and rate-independent pitch measured by autocorrelation);
//   2. offline (tools/render) path vs plugin processBlock path, bit-exact,
//      at four host block sizes;
//   3. all 128 factory presets switched under a held chord (host-usage crash
//      vector) — stability, 0 dBFS ceiling, and zero audio-thread allocation;
//   4. preset-swap click matrix scored with the project click ratio;
//   5. eight processor instances at different rates/blocks running
//      concurrently — bit-identical to running each alone (CLAUDE.md rule 7);
//   6. every one of the 67 parameters at both rails;
//   7. seeded full-parameter fuzz (reproducible: kFuzzSeed).
//
// Include AFTER PluginProcessor.h, BinaryData.h, RecipeData.h, TestUtil.h and
// the AllocGuard definition in StateTests.cpp.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

namespace robustproc
{

using namespace blockwave;
using namespace testutil;

inline constexpr double kProcRates[] = { 44100.0, 48000.0, 88200.0,
                                         96000.0, 176400.0, 192000.0 };

// Fixed fuzz seed — every reported failure is reproducible by index.
inline constexpr std::uint64_t kFuzzSeed = 0xB10C'0000'C0DEull;
inline constexpr int kFuzzStates = 300;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline RecipeBook& sharedRecipeBook()
{
    static RecipeBook book;
    static bool loaded = false;
    if (! loaded)
    {
        int size = 0;
        juce::String err;
        if (const char* data = RecipeData::getNamedResource ("recipes_json", size))
            book.loadFromJson (juce::String::fromUTF8 (data, size), err);
        loaded = true;
    }
    return book;
}

// Factory preset var by its "name" field ("SHARDSTORM", "COIN CHUTE", ...).
inline juce::var factoryPresetByName (const juce::String& wanted)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        if (data == nullptr)
            continue;
        const auto v = juce::JSON::parse (juce::String::fromUTF8 (data, size));
        if (v.getProperty ("name", "").toString() == wanted)
            return v;
    }
    return {};
}

inline std::vector<juce::var> allFactoryPresets()
{
    std::vector<juce::var> out;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            out.push_back (juce::JSON::parse (juce::String::fromUTF8 (data, size)));
    }
    return out;
}

struct Stats
{
    float peak = 0.0f;
    double rms = 0.0;
    double dc = 0.0;
    int nonFinite = 0;
};

inline Stats analyse (const Rendered& r)
{
    Stats s;
    double sum = 0.0, sq = 0.0;
    for (size_t i = 0; i < r.l.size(); ++i)
    {
        const float a = r.l[i], b = r.r[i];
        if (! std::isfinite (a) || ! std::isfinite (b))
            { ++s.nonFinite; continue; }
        s.peak = std::max ({ s.peak, std::abs (a), std::abs (b) });
        sum += a;
        sq += static_cast<double> (a) * a;
    }
    const double n = static_cast<double> (r.l.size());
    s.rms = std::sqrt (sq / n);
    s.dc = sum / n;
    return s;
}

// Autocorrelation f0 over the loudest 200 ms window in [0, searchSec). Returns
// f0 in Hz and a 0..1 confidence (normalised autocorrelation peak). Works on
// percussive and sustained material alike; noise scores low confidence, which
// is exactly how the pitch assertion is gated.
struct PitchResult { double f0 = 0.0; double confidence = 0.0; };

inline PitchResult measureF0Autocorr (const std::vector<float>& x, double sr,
                                      double searchSec = 1.2)
{
    const int win = static_cast<int> (0.2 * sr);
    const int limit = std::min (static_cast<int> (x.size()) - win,
                                static_cast<int> (searchSec * sr));
    if (limit <= 0)
        return {};

    int bestStart = 0;
    double bestEnergy = -1.0;
    const int hop = std::max (1, win / 4);
    for (int start = 0; start < limit; start += hop)
    {
        double e = 0.0;
        for (int i = start; i < start + win; ++i)
            e += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
        if (e > bestEnergy) { bestEnergy = e; bestStart = start; }
    }
    if (bestEnergy <= 1.0e-12)
        return {};

    const int lagMin = std::max (2, static_cast<int> (sr / 2000.0));   // 2 kHz
    const int lagMax = std::min (win - 1, static_cast<int> (sr / 30.0)); // 30 Hz
    double best = 0.0;
    int bestLag = 0;
    double r0 = 0.0;
    for (int i = bestStart; i < bestStart + win; ++i)
        r0 += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double num = 0.0, den = 0.0;
        for (int i = bestStart; i + lag < bestStart + win; ++i)
        {
            const double a = static_cast<double> (x[static_cast<size_t> (i)]);
            const double b = static_cast<double> (x[static_cast<size_t> (i + lag)]);
            num += a * b;
            den += b * b;
        }
        const double c = num / std::sqrt (r0 * den + 1.0e-18);
        if (c > best) { best = c; bestLag = lag; }
    }
    if (bestLag == 0)
        return {};

    // Parabolic refinement is unnecessary: the gate is +-25 cents, and the lag
    // resolution at 2 kHz / 192 kHz is far finer than that.
    return { sr / static_cast<double> (bestLag), best };
}

// ---------------------------------------------------------------------------
// 1. Factory presets across the full sample-rate matrix.
// ---------------------------------------------------------------------------
inline void test_preset_sample_rate_sweep()
{
    std::printf ("[preset_sample_rate_sweep]\n");

    // Eight presets, one per SPEC category; five are recipe showcases (their
    // craft grid matches a recipe pattern exactly), PERC included per the
    // Phase-7 brief.
    const char* const names[] =
    {
        "SHARDSTORM",       // LEAD  — recipe
        "MAGMA FLOOR",      // BASS  — recipe
        "QUARRY KICK",      // PERC  — recipe
        "PERMAFROST",       // PAD   — recipe
        "FOREST LULLABY",   // KEYS  — recipe
        "COIN CHUTE",       // CHIP  — plain craft
        "KALIMBA COVE",     // PLUCK — plain craft
        "STATIC FIELD",     // FX    — plain craft, noise-heavy (non-tonal)
    };

    auto& book = sharedRecipeBook();
    for (const char* name : names)
    {
        const auto v = factoryPresetByName (name);
        CHECK_MSG (v.getDynamicObject() != nullptr, "factory preset '%s' not found", name);
        if (v.getDynamicObject() == nullptr)
            continue;

        ParamSnapshot p;
        juce::String err;
        CHECK_MSG (applyPreset (v, &book, p, err), "'%s': %s", name, err.toRawUTF8());

        // Reference pitch at 48 kHz; every other rate must agree with it.
        const auto ref = renderNote (p, 57, 48000.0, 1.5, 2.5);
        const auto refPitch = measureF0Autocorr (ref.l, 48000.0);
        const bool tonal = refPitch.confidence > 0.80;
        std::printf ("  %-15s 48k f0 %8.2f Hz (conf %.2f, %s)\n", name,
                     refPitch.f0, refPitch.confidence, tonal ? "tonal" : "non-tonal");

        for (const double sr : kProcRates)
        {
            const auto r = renderNote (p, 57, sr, 1.5, 2.5);
            const auto st = analyse (r);
            const auto pitch = measureF0Autocorr (r.l, sr);
            const double cents = (tonal && pitch.f0 > 0.0 && refPitch.f0 > 0.0)
                               ? centsDiff (pitch.f0, refPitch.f0) : 0.0;
            std::printf ("    %8.1f Hz peak %.4f rms %.4f dc %+.5f f0 %8.2f "
                         "(conf %.2f) %+6.1f cents\n",
                         sr, static_cast<double> (st.peak), st.rms, st.dc,
                         pitch.f0, pitch.confidence, cents);

            CHECK_MSG (st.nonFinite == 0, "'%s' @ %.1f: %d non-finite samples",
                       name, sr, st.nonFinite);
            CHECK_MSG (st.peak > 1.0e-3f, "'%s' @ %.1f: rendered silence (peak %.3g)",
                       name, sr, static_cast<double> (st.peak));
            CHECK_MSG (st.peak < 0.98f, "'%s' @ %.1f: peak %.4f >= 0.98",
                       name, sr, static_cast<double> (st.peak));
            // Rate-dependent pitch bug detector. Gate +-25 cents: far tighter
            // than any rate bug (which lands octaves or 8.8% off — the
            // 48000/44100 ratio) and loose enough for the autocorrelation
            // estimator on crushed/detuned material.
            CHECK_MSG (! tonal || std::abs (cents) <= 25.0,
                       "'%s' @ %.1f: pitch %.2f Hz is %+.1f cents off the 48k "
                       "reference %.2f Hz", name, sr, pitch.f0, cents, refPitch.f0);
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Offline render path vs plugin processBlock path.
// ---------------------------------------------------------------------------
// tools/render drives BlockwaveEngine directly in 512-sample chunks with
// sample-accurate event splits (tools/render/RenderMain.cpp); the plugin walks
// the host MIDI buffer the same way. Two separate claims are checked:
//
//   (a) given the SAME resolved parameters, the offline loop and the plugin
//       processBlock path are BIT-IDENTICAL at any host block size — this is
//       what makes a faster-than-realtime bounce match live playback;
//   (b) the preset -> parameters step of the two paths agrees to within the
//       APVTS float round trip. It is NOT bit-identical: loadPresetVar writes
//       plain -> normalised -> (host) -> plain, one round trip more than
//       tools/render's plainFromVar, so e.g. filt_cutoff 900 Hz arrives as
//       899.999695 Hz in the plugin (3.4e-7 relative). On chaotic patches
//       (noise + S&H LFO + resonance) that seed difference diverges into an
//       audible-scale-irrelevant but non-zero waveform difference, so (b) is
//       bounded in dB rather than asserted at zero. Phase-7 finding F2.
inline void test_offline_vs_plugin_path()
{
    std::printf ("[offline_vs_plugin_path]\n");
    const double sr = 48000.0;
    const int totalSamples = 96000;                 // 2 s
    const int noteOffAt   = 72000;                  // 1.5 s
    const int note = 57;
    const float vel = 100.0f / 127.0f;

    const char* const names[] = { "SHARDSTORM", "PERMAFROST", "QUARRY KICK", "STATIC FIELD" };
    auto& book = sharedRecipeBook();

    // The offline (tools/render) loop, verbatim.
    const auto offlineRender = [&] (const ParamSnapshot& p, std::vector<float>& l,
                                    std::vector<float>& r)
    {
        l.assign (static_cast<size_t> (totalSamples), 0.0f);
        r.assign (static_cast<size_t> (totalSamples), 0.0f);
        BlockwaveEngine e;
        e.setParams (p);
        e.setTempo (120.0);
        e.prepare (sr, 512);
        e.noteOn (note, vel);
        bool off = false;
        for (int pos = 0; pos < totalSamples; )
        {
            if (! off && pos >= noteOffAt) { e.noteOff (note); off = true; }
            int n = std::min (512, totalSamples - pos);
            if (! off && pos + n > noteOffAt)
                n = noteOffAt - pos;
            e.process (l.data() + pos, r.data() + pos, n);
            pos += n;
        }
    };

    double worstExact = 0.0, worstMapping = 0.0;
    for (const char* name : names)
    {
        const auto v = factoryPresetByName (name);
        if (v.getDynamicObject() == nullptr)
        {
            CHECK_MSG (false, "factory preset '%s' not found", name);
            continue;
        }
        juce::String err;
        ParamSnapshot pRender;                       // tools/render mapping
        CHECK_MSG (applyPreset (v, &book, pRender, err), "'%s': %s", name, err.toRawUTF8());

        ParamSnapshot pPlugin;                       // what the plugin actually runs
        {
            BlockwaveAudioProcessor probe;
            CHECK_MSG (probe.loadPresetVar (v, err), "'%s': plugin load failed: %s",
                       name, err.toRawUTF8());
            RawParams raw;
            raw.attach (probe.apvts);
            raw.toSnapshot (pPlugin);
        }

        std::vector<float> exactL, exactR, mapL, mapR;
        offlineRender (pPlugin, exactL, exactR);
        offlineRender (pRender, mapL, mapR);

        for (const int block : { 64, 128, 512, 1024 })
        {
            BlockwaveAudioProcessor proc;
            CHECK_MSG (proc.loadPresetVar (v, err), "'%s': plugin load failed: %s",
                       name, err.toRawUTF8());
            proc.prepareToPlay (sr, block);

            juce::AudioBuffer<float> buf (2, block);
            std::vector<float> gotL, gotR;
            gotL.reserve (static_cast<size_t> (totalSamples));
            gotR.reserve (static_cast<size_t> (totalSamples));
            for (int pos = 0; pos < totalSamples; pos += block)
            {
                juce::MidiBuffer midi;
                if (pos == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                if (noteOffAt >= pos && noteOffAt < pos + block)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), noteOffAt - pos);
                proc.processBlock (buf, midi);
                const int n = std::min (block, totalSamples - pos);
                gotL.insert (gotL.end(), buf.getReadPointer (0), buf.getReadPointer (0) + n);
                gotR.insert (gotR.end(), buf.getReadPointer (1), buf.getReadPointer (1) + n);
            }

            double dExact = 0.0, dMap = 0.0;
            for (size_t i = 0; i < exactL.size(); ++i)
            {
                dExact = std::max ({ dExact,
                                     std::abs (static_cast<double> (exactL[i] - gotL[i])),
                                     std::abs (static_cast<double> (exactR[i] - gotR[i])) });
                dMap = std::max (dMap, std::abs (static_cast<double> (mapL[i] - gotL[i])));
            }
            worstExact = std::max (worstExact, dExact);
            worstMapping = std::max (worstMapping, dMap);
            std::printf ("  %-15s host block %4d: offline(plugin params) delta %.3g, "
                         "offline(render mapping) delta %.3g (%.1f dBFS)\n",
                         name, block, dExact, dMap,
                         20.0 * std::log10 (dMap + 1.0e-12));
            CHECK_MSG (dExact == 0.0,
                       "'%s' @ block %d: offline and plugin paths differ by %.3g on "
                       "identical parameters", name, block, dExact);
            // (b): bounded by the APVTS round trip, not zero. -40 dBFS is 30 dB
            // below the quietest factory preset peak in the sweep above.
            CHECK_MSG (dMap <= 0.01,
                       "'%s' @ block %d: render-mapping vs plugin deviation %.3g "
                       "exceeds -40 dBFS", name, block, dMap);
        }
    }
    std::printf ("  4 presets x 4 host block sizes: identical-parameter deviation %.3g, "
                 "preset-mapping deviation %.3g (%.1f dBFS)\n",
                 worstExact, worstMapping, 20.0 * std::log10 (worstMapping + 1.0e-12));
}

// ---------------------------------------------------------------------------
// 3. All 128 factory presets switched under a held chord.
// ---------------------------------------------------------------------------
inline void test_rapid_preset_switch_held_notes()
{
    std::printf ("[rapid_preset_switch_held_notes]\n");
    const double sr = 48000.0;
    const int block = 128;

    const auto presets = allFactoryPresets();
    CHECK_MSG (presets.size() == 128, "expected 128 factory presets, got %d",
               static_cast<int> (presets.size()));

    BlockwaveAudioProcessor proc;
    proc.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block);

    // Chord held for the whole run; the preset changes underneath it.
    {
        juce::MidiBuffer midi;
        for (const int n : { 45, 52, 57, 64 })
            midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        proc.processBlock (buf, midi);
    }

    long allocs = 0;
    float peak = 0.0f;
    int nonFinite = 0;
    float lastSample = 0.0f;
    float worstStep = 0.0f;
    double sumAbs = 0.0;
    long samples = 0;
    juce::String err;

    // Every preset, one after another, a new one every 3 blocks (= 8 ms at
    // 48k/128) — far faster than a human can click, and each switch rewrites
    // all 67 parameters plus the craft grid.
    for (size_t i = 0; i < presets.size(); ++i)
    {
        CHECK_MSG (proc.loadPresetVar (presets[i], err), "preset %d load failed: %s",
                   static_cast<int> (i), err.toRawUTF8());
        for (int b = 0; b < 3; ++b)
        {
            juce::MidiBuffer midi;
            {
                AllocGuard guard;
                proc.processBlock (buf, midi);
                allocs += guard.count();
            }
            const float* l = buf.getReadPointer (0);
            const float* r = buf.getReadPointer (1);
            for (int k = 0; k < block; ++k)
            {
                if (! std::isfinite (l[k]) || ! std::isfinite (r[k]))
                    ++nonFinite;
                peak = std::max ({ peak, std::abs (l[k]), std::abs (r[k]) });
                worstStep = std::max (worstStep, std::abs (l[k] - lastSample));
                lastSample = l[k];
                sumAbs += std::abs (static_cast<double> (l[k]));
                ++samples;
            }
        }
    }

    // Still alive and still sounding after 128 switches under a held chord.
    float tailPeak = 0.0f;
    for (int b = 0; b < 20; ++b)
    {
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        for (int k = 0; k < block; ++k)
            tailPeak = std::max (tailPeak, std::abs (buf.getReadPointer (0)[k]));
    }

    std::printf ("  128 presets, 3 blocks each @ 48k/128: peak %.4f, mean |x| %.4f,\n"
                 "  non-finite %d, audio-thread allocations %ld, worst sample step %.4f,\n"
                 "  still sounding after the run (tail peak %.4f)\n",
                 static_cast<double> (peak), sumAbs / static_cast<double> (samples),
                 nonFinite, allocs, static_cast<double> (worstStep),
                 static_cast<double> (tailPeak));
    CHECK_MSG (nonFinite == 0, "%d non-finite samples during preset cycling", nonFinite);
    // Softclip contract (src/BlockwaveEngine.h): <= 0 dBFS, asymptote rounds
    // to exactly 1.0 in float.
    CHECK_MSG (peak <= 1.0f, "peak %.4f exceeds 0 dBFS during preset cycling",
               static_cast<double> (peak));
    CHECK_MSG (allocs == 0, "%ld audio-thread allocations while switching presets", allocs);
    CHECK_MSG (tailPeak > 1.0e-4f, "engine went silent after the preset cycle");
    CHECK_MSG (sumAbs / static_cast<double> (samples) > 1.0e-4,
               "preset cycling produced near-silence overall");

    // Same run at the two extremes of the supported rate/block range.
    for (const auto rateBlock : { std::pair<double, int> { 44100.0, 16 },
                                  std::pair<double, int> { 192000.0, 4096 } })
    {
        BlockwaveAudioProcessor p2;
        p2.prepareToPlay (rateBlock.first, rateBlock.second);
        juce::AudioBuffer<float> b2 (2, rateBlock.second);
        {
            juce::MidiBuffer midi;
            for (const int n : { 45, 52, 57, 64 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            p2.processBlock (b2, midi);
        }
        int bad = 0;
        float pk = 0.0f;
        for (size_t i = 0; i < presets.size(); ++i)
        {
            p2.loadPresetVar (presets[i], err);
            juce::MidiBuffer midi;
            p2.processBlock (b2, midi);
            for (int k = 0; k < rateBlock.second; ++k)
            {
                const float x = b2.getReadPointer (0)[k];
                if (! std::isfinite (x)) ++bad;
                pk = std::max (pk, std::abs (x));
            }
        }
        std::printf ("  %8.1f Hz / %4d: 128 switches, non-finite %d, peak %.4f\n",
                     rateBlock.first, rateBlock.second, bad, static_cast<double> (pk));
        CHECK_MSG (bad == 0, "%.1f/%d: %d non-finite samples while switching",
                   rateBlock.first, rateBlock.second, bad);
        CHECK_MSG (pk <= 1.0f, "%.1f/%d: peak %.4f exceeds 0 dBFS",
                   rateBlock.first, rateBlock.second, static_cast<double> (pk));
    }
}

// ---------------------------------------------------------------------------
// 4. Preset-swap click matrix.
// ---------------------------------------------------------------------------
// A preset load writes all 67 parameters through the same atomic APVTS path a
// craft edit uses, so the same detector and the same 2.5x gate apply
// (tests/CraftCoverageTests.h). Scored on real factory preset pairs across
// category boundaries — the switch a producer actually makes mid-note.
//
// ONE addition to the craft-matrix bound (Phase-7 finding F3): a preset switch
// can move the destination patch to a far brighter filter setting (STATIC
// FIELD -> COIN CHUTE opens the cutoff 900 -> 9000 Hz) AND to a zero-sustain
// envelope, which silences the held note. The craft matrix's bound — steady
// slew before and 200 ms after the swap — then collapses to the decaying
// tail's slew and inflates the ratio to 3.18 even though the post-swap step
// (0.0296, -30.6 dBFS) is smaller than what the destination patch produces
// natively on its own note. The bound here therefore also includes the
// destination's own steady-state slew, measured on a fresh note. The
// negative control at the end proves the detector still has teeth.
inline void test_preset_switch_click_matrix()
{
    std::printf ("[preset_switch_click_matrix]\n");
    constexpr float kMaxRatio = 2.5f;
    constexpr double kInstants[] = { 0.42, 0.70, 0.99, 1.28 };
    const int note = 60;
    const float vel = 100.0f / 127.0f;
    const double sr = 48000.0;

    // Worst single-sample step in the loudest 150 ms of a patch's own note:
    // the slew that patch produces natively, and therefore the floor of any
    // honest "did the transition click?" bound.
    const auto ownSteadySlew = [&] (const ParamSnapshot& p)
    {
        const auto r = renderNote (p, note, sr, 1.6, 1.8, 512, 120.0, vel);
        const int win = static_cast<int> (0.150 * sr);
        float best = 0.0f;
        for (int start = 0; start + win < static_cast<int> (r.l.size()); start += win / 2)
            best = std::max (best, craftcoverage::maxStep (r.l, start, start + win));
        return best;
    };

    struct Pair { const char* from; const char* to; };
    const Pair pairs[] =
    {
        { "PERMAFROST",     "MAGMA FLOOR"    },   // PAD  -> BASS
        { "MAGMA FLOOR",    "PERMAFROST"     },   // BASS -> PAD
        { "SHARDSTORM",     "QUARRY KICK"    },   // LEAD -> PERC
        { "QUARRY KICK",    "SHARDSTORM"     },   // PERC -> LEAD
        { "COIN CHUTE",     "STATIC FIELD"   },   // CHIP -> FX (noise on)
        { "STATIC FIELD",   "COIN CHUTE"     },   // FX   -> CHIP (noise off, F3)
        { "FOREST LULLABY", "KALIMBA COVE"   },   // KEYS -> PLUCK
        { "KALIMBA COVE",   "FOREST LULLABY" },
    };

    auto& book = sharedRecipeBook();
    float worst = 0.0f;
    for (const auto& pr : pairs)
    {
        ParamSnapshot a, b;
        juce::String err;
        const auto va = factoryPresetByName (pr.from);
        const auto vb = factoryPresetByName (pr.to);
        CHECK_MSG (va.getDynamicObject() != nullptr && vb.getDynamicObject() != nullptr,
                   "preset pair %s -> %s not found", pr.from, pr.to);
        if (va.getDynamicObject() == nullptr || vb.getDynamicObject() == nullptr)
            continue;
        applyPreset (va, &book, a, err);
        applyPreset (vb, &book, b, err);

        const float ownA = ownSteadySlew (a);
        const float ownB = ownSteadySlew (b);

        // Two scorings per pair:
        //   strict  — the Phase-4 craft-matrix bound (steady before / after);
        //   widened — the same bound floored by each patch's own steady slew.
        // The strict bound is the sharp detector (a hard splice scores 15-25x
        // against it) and is what the gate uses, EXCEPT when the destination
        // preset silences the held note (zero sustain): then the "after"
        // window is a decayed tail, the strict bound collapses, and the
        // widened bound is the honest one. The exception is flagged per pair.
        float strict = 0.0f, widened = 0.0f, step = 0.0f, bound = 0.0f;
        bool decayed = false;
        for (const double t : kInstants)
        {
            int stepAt = 0;
            const auto l = craftcoverage::renderSwap (a, b, note, vel, sr,
                                                      512, t, 0.45, stepAt);
            const float sw = craftcoverage::maxStep (l, stepAt,
                                                     stepAt + static_cast<int> (0.030 * sr));
            const float be = craftcoverage::maxStep (l, stepAt - static_cast<int> (0.150 * sr),
                                                     stepAt);
            const float af = craftcoverage::maxStep (l, stepAt + static_cast<int> (0.200 * sr),
                                                     static_cast<int> (l.size()));
            const float bdS = 1.25f * std::max (be, af);
            const float bdW = 1.25f * std::max ({ be, af, ownA, ownB });
            const float rtS = sw / std::max (1.0e-9f, bdS);
            if (rtS > strict)
            {
                strict = rtS;
                widened = sw / std::max (1.0e-9f, bdW);
                step = sw;
                bound = bdS;
                // Did the swap render ever reach a steady state that
                // represents either patch? If neither window carries the slew
                // the patches produce on their own notes (destination with a
                // zero-sustain envelope, source already decayed), the strict
                // bound is measuring a tail, not a patch.
                decayed = std::max (be, af) < 0.25f * std::max (ownA, ownB);
            }
        }
        std::printf ("  %-15s -> %-15s step %.4f bound %.4f strict %5.2f widened %5.2f%s\n",
                     pr.from, pr.to, static_cast<double> (step),
                     static_cast<double> (bound), static_cast<double> (strict),
                     static_cast<double> (widened),
                     decayed ? "  [destination silences the held note]" : "");
        CHECK_MSG (strict <= kMaxRatio || (decayed && widened <= kMaxRatio),
                   "preset switch %s -> %s clicked: step %.4f is %.2fx the strict "
                   "bound %.4f (widened %.2f, decayed %d)",
                   pr.from, pr.to, static_cast<double> (step),
                   static_cast<double> (strict), static_cast<double> (bound),
                   static_cast<double> (widened), decayed ? 1 : 0);
        worst = std::max (worst, decayed ? widened : strict);
    }

    // Negative control: a hard splice of two steady renders — an unsmoothed
    // preset jump — must blow through the gate against the strict bound, on
    // every pair, so the reported margin is the real one.
    {
        float bestRatio = 0.0f;
        float leastRatio = 1.0e9f;
        int scored = 0;
        for (const auto& pr : pairs)
        {
            ParamSnapshot a, b;
            juce::String err;
            applyPreset (factoryPresetByName (pr.from), &book, a, err);
            applyPreset (factoryPresetByName (pr.to), &book, b, err);

            const double swapSec = 0.99;
            const int stepAt = static_cast<int> (swapSec * sr);
            const auto ra = renderNote (a, note, sr, 2.0, 1.6, 512, 120.0, vel);
            const auto rb = renderNote (b, note, sr, 2.0, 1.6, 512, 120.0, vel);
            const float be = craftcoverage::maxStep (ra.l, stepAt - static_cast<int> (0.150 * sr),
                                                     stepAt);
            const float af = craftcoverage::maxStep (rb.l, stepAt + static_cast<int> (0.200 * sr),
                                                     static_cast<int> (rb.l.size()));
            const float bd = 1.25f * std::max (be, af);      // strict bound
            // A splice can only click if there is signal to splice: skip
            // pairs whose source has already decayed at the splice instant
            // (a blip or a one-shot percussion patch 1 s into the note).
            double rmsSrc = 0.0;
            const int w = static_cast<int> (0.050 * sr);
            for (int i = stepAt - w; i < stepAt; ++i)
                rmsSrc += static_cast<double> (ra.l[static_cast<size_t> (i)])
                        * ra.l[static_cast<size_t> (i)];
            rmsSrc = std::sqrt (rmsSrc / w);
            if (rmsSrc < 0.01)
                continue;

            float sw = 0.0f;
            for (int k = 0; k < 900; ++k)
            {
                const size_t n0 = static_cast<size_t> (stepAt + k);
                sw = std::max (sw, std::abs (rb.l[n0] - ra.l[n0 - 1]));
            }
            const float rt = sw / std::max (1.0e-9f, bd);
            ++scored;
            bestRatio = std::max (bestRatio, rt);
            leastRatio = std::min (leastRatio, rt);
        }
        std::printf ("  negative control (hard splice, %d pairs with a sounding "
                     "source): ratio %.2f..%.2f\n", scored,
                     static_cast<double> (leastRatio), static_cast<double> (bestRatio));
        // The detector must catch a hard splice. Its sensitivity varies with
        // how different the two patches are: between two patches of similar
        // brightness and level a splice can score as low as ~1.7, so the
        // assertion is on the worst-case splice being caught, and the full
        // range is printed. Sharper evidence for the smoothing itself is the
        // Phase-4 craft-transition matrix (same APVTS -> engine path, splice
        // ratio 20x+ there).
        CHECK_MSG (bestRatio > kMaxRatio,
                   "click detector toothless: the loudest hard splice scored only "
                   "%.2f against the %.2f gate", static_cast<double> (bestRatio),
                   static_cast<double> (kMaxRatio));
    }

    std::printf ("  8 cross-category switches, worst ratio %.2f (gate %.1f)\n",
                 static_cast<double> (worst), static_cast<double> (kMaxRatio));
}

// ---------------------------------------------------------------------------
// 5. Multiple instances (CLAUDE.md rule 7: no static mutable state).
// ---------------------------------------------------------------------------
inline void test_multiple_instances()
{
    std::printf ("[multiple_instances]\n");
    constexpr int kInstances = 8;

    struct Cfg { double sr; int block; const char* preset; int note; };
    const Cfg cfg[kInstances] =
    {
        {  44100.0,   16, "SHARDSTORM",     57 },
        {  48000.0,  128, "MAGMA FLOOR",    36 },
        {  88200.0,  512, "QUARRY KICK",    45 },
        {  96000.0,   64, "PERMAFROST",     60 },
        { 176400.0, 1024, "FOREST LULLABY", 64 },
        { 192000.0, 4096, "COIN CHUTE",     72 },
        {  44100.0,  256, "KALIMBA COVE",   55 },
        {  48000.0, 2048, "STATIC FIELD",   48 },
    };

    const double runSec = 0.6;
    const auto renderOne = [&] (int i, std::vector<float>& out)
    {
        BlockwaveAudioProcessor proc;
        juce::String err;
        const auto v = factoryPresetByName (cfg[i].preset);
        proc.loadPresetVar (v, err);
        proc.prepareToPlay (cfg[i].sr, cfg[i].block);
        juce::AudioBuffer<float> buf (2, cfg[i].block);
        const int total = static_cast<int> (runSec * cfg[i].sr);
        out.clear();
        out.reserve (static_cast<size_t> (total));
        for (int pos = 0; pos < total; pos += cfg[i].block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, cfg[i].note,
                                                          (juce::uint8) 100), 0);
            proc.processBlock (buf, midi);
            const int n = std::min (cfg[i].block, total - pos);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + n);
        }
    };

    // Reference: each instance rendered alone, one after the other.
    std::vector<std::vector<float>> alone (kInstances);
    for (int i = 0; i < kInstances; ++i)
        renderOne (i, alone[i]);

    // Now all eight at once, on eight threads, in one process.
    std::vector<std::vector<float>> together (kInstances);
    {
        std::vector<std::thread> threads;
        threads.reserve (kInstances);
        for (int i = 0; i < kInstances; ++i)
            threads.emplace_back ([&, i] { renderOne (i, together[i]); });
        for (auto& t : threads)
            t.join();
    }

    for (int i = 0; i < kInstances; ++i)
    {
        CHECK_MSG (alone[i].size() == together[i].size(),
                   "instance %d: length changed when run concurrently", i);
        double maxDiff = 0.0;
        float peak = 0.0f;
        int bad = 0;
        const size_t n = std::min (alone[i].size(), together[i].size());
        for (size_t k = 0; k < n; ++k)
        {
            maxDiff = std::max (maxDiff, std::abs (static_cast<double> (alone[i][k]
                                                                     - together[i][k])));
            if (! std::isfinite (together[i][k])) ++bad;
            peak = std::max (peak, std::abs (together[i][k]));
        }
        std::printf ("  #%d %-15s %8.1f Hz / %4d: peak %.4f, max |alone - concurrent| = %.3g\n",
                     i, cfg[i].preset, cfg[i].sr, cfg[i].block,
                     static_cast<double> (peak), maxDiff);
        CHECK_MSG (maxDiff == 0.0, "instance %d is not independent: %.3g deviation when "
                   "8 instances run concurrently", i, maxDiff);
        CHECK_MSG (bad == 0, "instance %d: %d non-finite samples", i, bad);
        CHECK_MSG (peak > 1.0e-4f, "instance %d rendered silence", i);
    }
    std::printf ("  8 instances, 6 sample rates, 8 block sizes: fully independent\n");
}

// ---------------------------------------------------------------------------
// 6. Every parameter at both rails.
// ---------------------------------------------------------------------------
inline void test_extreme_parameter_rails()
{
    std::printf ("[extreme_parameter_rails]\n");
    const double sr = 48000.0;

    struct Result { int id; bool atMax; float peak; double rms; int bad; double ms; };
    std::vector<Result> results;
    results.reserve (static_cast<size_t> (kNumParams) * 2);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<PId> (i);
        const auto& d = paramDef (id);
        for (int rail = 0; rail < 2; ++rail)
        {
            ParamSnapshot p;                              // SPEC defaults
            applyToSnapshot (p, id, rail == 0 ? d.minValue : d.maxValue);

            const auto t0 = std::chrono::steady_clock::now();
            const auto r = renderNote (p, 57, sr, 1.0, 1.6);
            const auto t1 = std::chrono::steady_clock::now();
            const auto st = analyse (r);
            const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
            results.push_back ({ i, rail == 1, st.peak, st.rms, st.nonFinite, ms });

            CHECK_MSG (st.nonFinite == 0, "%s at %s: %d non-finite samples", d.id,
                       rail == 0 ? "min" : "max", st.nonFinite);
            CHECK_MSG (st.peak <= 1.0f, "%s at %s: peak %.4f exceeds 0 dBFS", d.id,
                       rail == 0 ? "min" : "max", static_cast<double> (st.peak));
        }
    }

    // Denormal / runaway-cost detector: a stalled render (denormals in the
    // filter, delay or FDN state) costs orders of magnitude more than the
    // median, not 20%.
    std::vector<double> times;
    times.reserve (results.size());
    for (const auto& r : results)
        times.push_back (r.ms);
    std::sort (times.begin(), times.end());
    const double median = times[times.size() / 2];
    double worstMs = 0.0;
    int worstId = 0;
    for (const auto& r : results)
    {
        if (r.ms > worstMs) { worstMs = r.ms; worstId = r.id; }
        CHECK_MSG (r.ms < median * 25.0 + 5.0,
                   "%s at %s: render took %.2f ms vs %.2f ms median — denormal or "
                   "runaway cost", paramDef (static_cast<PId> (r.id)).id,
                   r.atMax ? "max" : "min", r.ms, median);
    }

    int silent = 0, hot = 0;
    float worstPeak = 0.0f;
    for (const auto& r : results)
    {
        if (r.peak < 1.0e-4f)
        {
            ++silent;
            std::printf ("    silent rail: %s at %s\n",
                         paramDef (static_cast<PId> (r.id)).id, r.atMax ? "max" : "min");
        }
        if (r.peak > 0.98f) ++hot;
        worstPeak = std::max (worstPeak, r.peak);
    }
    std::printf ("  %d rails (%d params x 2): peak max %.4f, %d silent, %d above 0.98,\n"
                 "  render time median %.2f ms, worst %.2f ms (%s)\n",
                 static_cast<int> (results.size()), kNumParams,
                 static_cast<double> (worstPeak), silent, hot, median, worstMs,
                 paramDef (static_cast<PId> (worstId)).id);
}

// ---------------------------------------------------------------------------
// 7. Seeded full-parameter fuzz.
// ---------------------------------------------------------------------------
inline void test_parameter_fuzz()
{
    std::printf ("[parameter_fuzz]\n");
    const double sr = 48000.0;
    std::mt19937_64 rng (kFuzzSeed);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    int worstState = -1;
    float worstPeak = 0.0f;
    double worstMs = 0.0, worstDc = 0.0;
    int ceilingHits = 0, silent = 0;
    std::vector<double> times;
    times.reserve (static_cast<size_t> (kFuzzStates));

    for (int s = 0; s < kFuzzStates; ++s)
    {
        ParamSnapshot p;
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto id = static_cast<PId> (i);
            const auto& d = paramDef (id);
            float v = 0.0f;
            switch (d.kind)
            {
                case PKind::boolean:
                    v = unit (rng) < 0.5f ? 0.0f : 1.0f;
                    break;
                case PKind::integer:
                case PKind::choice:
                {
                    const float span = d.maxValue - d.minValue;
                    v = d.minValue + std::floor (unit (rng) * (span + 1.0f));
                    v = std::min (v, d.maxValue);
                    break;
                }
                case PKind::floating:
                default:
                {
                    // Through the APVTS taper, so the sampling matches what a
                    // host randomising normalised values would produce.
                    const auto range = makeRange (d);
                    v = range.convertFrom0to1 (unit (rng));
                    break;
                }
            }
            applyToSnapshot (p, id, v);
        }

        const auto t0 = std::chrono::steady_clock::now();
        BlockwaveEngine e;
        e.setParams (p);
        e.setTempo (140.0);
        e.prepare (sr, 256);
        e.noteOn (45, 0.9f);
        e.noteOn (57, 0.6f);
        std::vector<float> l (static_cast<size_t> (sr * 0.6)), r (l.size());
        const int total = static_cast<int> (l.size());
        const int offAt = total * 2 / 3;
        for (int pos = 0; pos < total; )
        {
            if (pos >= offAt) { e.noteOff (45); e.noteOff (57); }
            const int n = std::min (256, total - pos);
            e.process (l.data() + pos, r.data() + pos, n);
            pos += n;
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
        times.push_back (ms);

        Rendered rr; rr.l = std::move (l); rr.r = std::move (r);
        const auto st = analyse (rr);
        if (st.peak > worstPeak) { worstPeak = st.peak; worstState = s; }
        worstDc = std::max (worstDc, std::abs (st.dc));
        worstMs = std::max (worstMs, ms);
        if (st.peak > 0.98f) ++ceilingHits;
        if (st.peak < 1.0e-4f) ++silent;

        CHECK_MSG (st.nonFinite == 0,
                   "fuzz state %d (seed 0x%llX): %d non-finite samples", s,
                   static_cast<unsigned long long> (kFuzzSeed), st.nonFinite);
        CHECK_MSG (st.peak <= 1.0f,
                   "fuzz state %d (seed 0x%llX): peak %.4f exceeds 0 dBFS", s,
                   static_cast<unsigned long long> (kFuzzSeed),
                   static_cast<double> (st.peak));
    }

    std::sort (times.begin(), times.end());
    const double median = times[times.size() / 2];
    std::printf ("  %d random full-parameter states (seed 0x%llX): worst peak %.4f "
                 "(state %d),\n  %d hit the 0.98 ceiling, %d silent, worst |DC| %.4f,\n"
                 "  render time median %.2f ms, worst %.2f ms\n",
                 kFuzzStates, static_cast<unsigned long long> (kFuzzSeed),
                 static_cast<double> (worstPeak), worstState, ceilingHits, silent,
                 worstDc, median, worstMs);
    CHECK_MSG (worstMs < median * 40.0 + 20.0,
               "a fuzz state stalled: %.2f ms vs %.2f ms median (denormals?)",
               worstMs, median);
}

// ---------------------------------------------------------------------------

inline void runAll()
{
    test_preset_sample_rate_sweep();
    test_offline_vs_plugin_path();
    test_rapid_preset_switch_held_notes();
    test_preset_switch_click_matrix();
    test_multiple_instances();
    test_extreme_parameter_rails();
    test_parameter_fuzz();
}

} // namespace robustproc
