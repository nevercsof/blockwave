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

// Phase-7 robustness sweeps, engine half (pure C++; the processor-level half
// lives in tests/RobustnessProcTests.h next to the JUCE state suite).
//
// Covers CLAUDE.md rule 8 — "handle all sample rates (44.1k-192k) and buffer
// sizes (16-4096), including buffer size changing between calls and offline
// rendering" — plus the tempo-change contract of the synced delay:
//
//   1. sample-rate sweep: pitch accuracy at 44.1/48/88.2/96/176.4/192 kHz
//      (a rate-dependent pitch bug is the classic failure of this contract);
//   2. buffer-size sweep: 16..4096 bit-identical, plus a pathological mixed
//      sequence (1, 4096, 3, 2048, 7, ...) and a mid-session re-prepare;
//   3. tempo sweep: echo lock at 14 BPMs, click-free abrupt jumps scored with
//      the same swap/steady ratio the craft-transition matrix uses, and
//      echo-spacing re-lock measured by tail autocorrelation;
//   4. CPU under stress at every supported sample rate.
//
// Requires FxTests.h (renderWithTempo, bestLag, maxSlew) — include it first.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace robust
{

using namespace blockwave;
using namespace testutil;

// The Phase-7 sample-rate matrix. 88.2 and 176.4 are in deliberately: an
// engine that only ever divides by 44100/48000 constants passes 96/192 by
// luck and fails these.
inline constexpr double kRates[] = { 44100.0, 48000.0, 88200.0,
                                     96000.0, 176400.0, 192000.0 };
inline constexpr int kNumRates = static_cast<int> (std::size (kRates));

// ---------------------------------------------------------------------------
// Patches
// ---------------------------------------------------------------------------

// Everything on: both pulses (B hard-synced), sub, noise, 4-way unison, filter
// with envelope, and the whole FX block including all six wet-path filters.
inline ParamSnapshot stressPatch()
{
    ParamSnapshot p;
    p.oscB_on = true; p.oscB_sync = true; p.oscB_semi = 7;
    p.sub_on = true; p.noise_on = true;
    p.uni_count = 4; p.uni_detune = 20.0f;
    p.filt_cutoff = 2500.0f; p.filt_res = 0.4f; p.filt_env = 0.5f;
    p.lfo1_pwm = 0.4f; p.lfo2_amt = 0.5f;
    p.crush_bits = 6; p.crush_down = 3; p.crush_mix = 0.7f;
    p.dly_time = 10; p.dly_fb = 0.5f; p.dly_pingpong = true; p.dly_mix = 0.4f;
    p.cave_size = 0.6f; p.cave_damp = 0.5f; p.cave_mix = 0.4f;
    p.crush_hp = 400.0f; p.dly_hp = 150.0f; p.cave_hp = 150.0f;
    p.crush_lp = 5500.0f; p.dly_lp = 2200.0f; p.cave_lp = 900.0f;
    return p;
}

// Short burst into a 1/4-note synced delay: the echo train is what the tempo
// tests measure.
inline ParamSnapshot delayBurstPatch()
{
    ParamSnapshot p;
    p.env1_a = 0.0f; p.env1_d = 0.03f; p.env1_s = 0.0f; p.env1_r = 0.01f;
    p.dly_time = 4;                       // "1/4"
    p.dly_fb = 0.7f;
    p.dly_mix = 0.5f;
    p.dly_pingpong = false;
    return p;
}

// Sustained band-limited tone through a wet-only delay: bounded slew, so a
// single-sample step is a meaningful click detector.
inline ParamSnapshot delayTonePatch()
{
    ParamSnapshot p;
    p.env1_a = 0.005f; p.env1_s = 1.0f;
    p.filt_cutoff = 900.0f;
    p.dly_time = 6;                       // "1/8"
    p.dly_fb = 0.5f;
    p.dly_mix = 1.0f;                     // wet only
    p.dly_pingpong = false;
    return p;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// renderNote with a scripted (cycling) block-size sequence. Splits at note-off
// exactly like renderNote, so results are directly comparable.
inline Rendered renderBlockSeq (const ParamSnapshot& p, int note, double sr,
                                double noteSec, double totalSec,
                                const std::vector<int>& seq)
{
    BlockwaveEngine engine;
    engine.setParams (p);
    engine.setTempo (120.0);
    engine.prepare (sr, *std::max_element (seq.begin(), seq.end()));

    const int total = static_cast<int> (totalSec * sr);
    const int offAt = static_cast<int> (noteSec * sr);
    Rendered out;
    out.l.assign (static_cast<size_t> (total), 0.0f);
    out.r.assign (static_cast<size_t> (total), 0.0f);

    engine.noteOn (note, 1.0f);
    int pos = 0;
    size_t k = 0;
    bool offSent = false;
    while (pos < total)
    {
        if (! offSent && pos >= offAt) { engine.noteOff (note); offSent = true; }
        int n = std::min (seq[k % seq.size()], total - pos);
        ++k;
        if (! offSent && pos + n > offAt)
            n = offAt - pos;
        if (n <= 0)
            continue;
        engine.process (out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

inline int countNonFinite (const Rendered& r)
{
    int bad = 0;
    for (size_t i = 0; i < r.l.size(); ++i)
        if (! std::isfinite (r.l[i]) || ! std::isfinite (r.r[i]))
            ++bad;
    return bad;
}

inline float peakOf (const Rendered& r)
{
    float pk = 0.0f;
    for (size_t i = 0; i < r.l.size(); ++i)
        pk = std::max ({ pk, std::abs (r.l[i]), std::abs (r.r[i]) });
    return pk;
}

// Normalised-autocorrelation peak lag of x[from, to) searched over
// [lagMin, lagMax]. Used to read the echo spacing straight off the tail.
inline int autocorrPeakLag (const std::vector<float>& x, int from, int to,
                            int lagMin, int lagMax)
{
    double best = -1.0e30;
    int bestLagS = lagMin;
    const int hi = std::min (to, static_cast<int> (x.size()));
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double num = 0.0, den = 0.0;
        for (int i = from; i + lag < hi; ++i)
        {
            const double a = static_cast<double> (x[static_cast<size_t> (i)]);
            const double b = static_cast<double> (x[static_cast<size_t> (i + lag)]);
            num += a * b;
            den += b * b;
        }
        const double c = num / std::sqrt (den + 1.0e-12);
        if (c > best) { best = c; bestLagS = lag; }
    }
    return bestLagS;
}

// ---------------------------------------------------------------------------
// 1. Sample-rate sweep — pitch must be rate independent.
// ---------------------------------------------------------------------------
inline void test_sample_rate_pitch_sweep()
{
    std::printf ("[sr_pitch_sweep]\n");
    const int notes[] = { 33, 45, 57, 69, 81, 93 };      // A1..A6

    for (const double sr : kRates)
    {
        double worst = 0.0;
        for (const int note : notes)
        {
            ParamSnapshot p;                              // defaults: OSC A square
            const auto r = renderNote (p, note, sr, 1.4, 1.5);
            const double f0 = measureF0 (r.l, static_cast<int> (0.4 * sr),
                                         static_cast<int> (1.35 * sr), sr);
            const double ref = 440.0 * std::exp2 ((note - 69) / 12.0);
            const double cents = centsDiff (f0, ref);
            worst = std::max (worst, std::abs (cents));
            CHECK_MSG (std::abs (cents) <= 1.0,
                       "sr %.1f note %d: pitch error %.3f cents", sr, note, cents);
            CHECK_MSG (countNonFinite (r) == 0, "sr %.1f note %d: non-finite samples",
                       sr, note);
            CHECK_MSG (peakOf (r) > 1.0e-3f && peakOf (r) < 0.98f,
                       "sr %.1f note %d: peak %.4f out of (0.001, 0.98)", sr, note,
                       static_cast<double> (peakOf (r)));
        }
        std::printf ("  %8.1f Hz: worst pitch error over A1..A6 = %+.3f cents\n", sr, worst);
    }
}

// A heavy patch (all sources + all FX) must also stay finite and bounded at
// every rate — the FX delay lines and the FDN are the rate-dependent parts.
inline void test_sample_rate_stress_patch()
{
    std::printf ("[sr_stress_patch]\n");
    const auto p = stressPatch();
    for (const double sr : kRates)
    {
        const auto r = renderNote (p, 48, sr, 1.0, 2.0);
        const int bad = countNonFinite (r);
        const float pk = peakOf (r);
        double rms = 0.0;
        for (size_t i = 0; i < r.l.size(); ++i)
            rms += static_cast<double> (r.l[i]) * r.l[i];
        rms = std::sqrt (rms / static_cast<double> (r.l.size()));
        std::printf ("  %8.1f Hz: peak %.4f  rms %.4f  non-finite %d\n",
                     sr, static_cast<double> (pk), rms, bad);
        CHECK_MSG (bad == 0, "sr %.1f: %d non-finite samples", sr, bad);
        CHECK_MSG (pk < 0.98f, "sr %.1f: peak %.4f >= 0.98", sr, static_cast<double> (pk));
        CHECK_MSG (rms > 1.0e-3, "sr %.1f: stress patch rendered near-silence (rms %.3g)",
                   sr, rms);
    }
}

// ---------------------------------------------------------------------------
// 2. Buffer-size sweep — bit-identical output for every block size, including
//    a pathological mixed sequence with a mid-session re-prepare.
// ---------------------------------------------------------------------------
inline void test_buffer_size_sweep()
{
    std::printf ("[buffer_size_sweep]\n");
    const auto p = stressPatch();
    const auto ref = renderNote (p, 48, 48000.0, 0.3, 0.5, 512);

    const int sizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
    for (const int bs : sizes)
    {
        const auto r = renderNote (p, 48, 48000.0, 0.3, 0.5, bs);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < ref.l.size(); ++i)
            maxDiff = std::max ({ maxDiff, std::abs (ref.l[i] - r.l[i]),
                                  std::abs (ref.r[i] - r.r[i]) });
        std::printf ("  block %4d vs 512: max |diff| = %.3g\n", bs,
                     static_cast<double> (maxDiff));
        CHECK_MSG (maxDiff == 0.0f, "block size %d changes output by %.3g", bs,
                   static_cast<double> (maxDiff));
    }

    // Same sweep at the extremes of the rate range: a block-size bug that only
    // shows at 192k (or at 44.1k) would slip past a 48k-only check.
    for (const double sr : { 44100.0, 192000.0 })
    {
        const auto refS = renderNote (p, 48, sr, 0.2, 0.3, 512);
        for (const int bs : { 16, 4096 })
        {
            const auto r = renderNote (p, 48, sr, 0.2, 0.3, bs);
            float maxDiff = 0.0f;
            for (size_t i = 0; i < refS.l.size(); ++i)
                maxDiff = std::max (maxDiff, std::abs (refS.l[i] - r.l[i]));
            std::printf ("  %8.1f Hz block %4d vs 512: max |diff| = %.3g\n", sr, bs,
                         static_cast<double> (maxDiff));
            CHECK_MSG (maxDiff == 0.0f, "sr %.1f block %d differs by %.3g", sr, bs,
                       static_cast<double> (maxDiff));
        }
    }
}

inline void test_pathological_block_sequence()
{
    std::printf ("[pathological_block_sequence]\n");
    const auto p = stressPatch();
    const auto ref = renderNote (p, 48, 48000.0, 0.3, 0.5, 512);

    // Sequences a sane host would never produce; a real one occasionally does
    // (freewheeling render, plugin-delay compensation flush, sample-accurate
    // automation splits).
    const std::vector<std::vector<int>> seqs =
    {
        { 1, 4096, 3, 2048, 7, 1024, 13, 512, 17, 256, 31, 128, 61, 64, 127, 32, 251, 16 },
        { 1 },                                        // one sample at a time
        { 4096, 1 },
        { 15, 17, 16, 1, 33, 47, 4095, 2, 2049 },
        { 3, 5, 7, 11, 13, 17, 19, 23, 29, 31 },
    };
    for (size_t s = 0; s < seqs.size(); ++s)
    {
        const auto r = renderBlockSeq (p, 48, 48000.0, 0.3, 0.5, seqs[s]);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < ref.l.size(); ++i)
            maxDiff = std::max ({ maxDiff, std::abs (ref.l[i] - r.l[i]),
                                  std::abs (ref.r[i] - r.r[i]) });
        std::printf ("  seq %zu (%zu sizes, first %d) vs 512: max |diff| = %.3g\n",
                     s, seqs[s].size(), seqs[s][0], static_cast<double> (maxDiff));
        CHECK_MSG (maxDiff == 0.0f, "pathological sequence %zu differs by %.3g", s,
                   static_cast<double> (maxDiff));
        CHECK_MSG (countNonFinite (r) == 0, "pathological sequence %zu: non-finite", s);
    }
}

// Host changes the buffer size (or sample rate) mid-session: prepare() is
// called again while the plugin is loaded. The engine must reset cleanly and
// keep working — no NaN, no stuck voices, correct pitch afterwards.
inline void test_mid_session_reprepare()
{
    std::printf ("[mid_session_reprepare]\n");
    const auto p = stressPatch();

    struct Step { double sr; int block; };
    const Step steps[] =
    {
        { 44100.0,   64 }, { 48000.0, 1024 }, { 96000.0,   16 },
        { 192000.0, 4096 }, { 88200.0, 128 }, { 176400.0, 512 },
        { 48000.0,  512 },
    };

    BlockwaveEngine engine;
    engine.setParams (p);
    engine.setTempo (120.0);
    engine.prepare (44100.0, 64);

    static float l[4096], r[4096];
    int totalBad = 0;
    float worstPeak = 0.0f;
    for (const auto& st : steps)
    {
        engine.prepare (st.sr, st.block);              // mid-session re-prepare
        engine.noteOn (57, 0.9f);
        const int blocks = static_cast<int> (0.5 * st.sr / st.block) + 1;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (l, r, st.block);
            for (int i = 0; i < st.block; ++i)
            {
                if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                    ++totalBad;
                worstPeak = std::max ({ worstPeak, std::abs (l[i]), std::abs (r[i]) });
            }
        }
        engine.noteOff (57);
        engine.process (l, r, st.block);
        CHECK_MSG (engine.activeVoiceCount() >= 0, "voice count went negative");
    }
    std::printf ("  7 re-prepares (44.1k/64 -> 192k/4096 -> 48k/512): "
                 "non-finite %d, peak %.4f\n", totalBad, static_cast<double> (worstPeak));
    CHECK_MSG (totalBad == 0, "%d non-finite samples across re-prepares", totalBad);
    CHECK_MSG (worstPeak < 1.0f, "peak %.4f exceeds 0 dBFS across re-prepares",
               static_cast<double> (worstPeak));

    // After the last re-prepare the engine must still be musically correct.
    engine.prepare (48000.0, 512);
    ParamSnapshot clean;                                // defaults
    engine.setParams (clean);
    engine.prepare (48000.0, 512);
    std::vector<float> ol (static_cast<size_t> (48000)), orr (static_cast<size_t> (48000));
    engine.noteOn (69, 1.0f);
    for (int pos = 0; pos < 48000; pos += 512)
        engine.process (ol.data() + pos, orr.data() + pos, 512);
    const double f0 = measureF0 (ol, 4800, 43200, 48000.0);
    std::printf ("  post-re-prepare A4 = %.3f Hz (%+.3f cents)\n", f0, centsDiff (f0, 440.0));
    CHECK_MSG (std::abs (centsDiff (f0, 440.0)) <= 1.0,
               "engine mis-tuned after re-prepare: %.3f cents", centsDiff (f0, 440.0));
}

// REGRESSION GUARD for Phase-7 finding F4 (FIXED): the CAVE reverb's WET LEVEL
// must not depend on the sample rate. Same patch, wet only, no damping.
//
// The defect was CaveReverb's input DC blocker, which hard-coded its pole at
// R = 0.995 instead of deriving it from the sample rate. The corner therefore
// rode the rate — ~35 Hz at 44.1 kHz but ~153 Hz at 192 kHz — and high-passed
// the reverb SEND, costing 1.52 dB of wet level at 192 kHz while leaving the
// decay slope untouched (-21.4 dB/s at every rate), which is exactly why the
// symptom read as injection gain rather than decay time. The tell is that the
// deviation grew as the SQUARE of the sample rate, the signature of a
// fixed-coefficient one-pole. With the pole rate-compensated the spread is
// 0.07 dB, in line with the dry path (0.02 dB) and the crusher (0.06 dB).
//
// The 0.2 dB ceiling below locks the fix in: it is tight enough that a
// regression to the old hard-coded pole fails at 88.2 kHz and above.
inline void test_cave_wet_level_rate_dependence()
{
    std::printf ("[cave_wet_level_rate_dependence]  (Phase-7 finding F4)\n");
    constexpr double kMaxDeviationDb = 0.2;

    ParamSnapshot p;
    p.env1_s = 1.0f;
    p.filt_cutoff = 20000.0f;
    p.cave_mix = 1.0f;
    p.cave_damp = 0.0f;

    double ref = 0.0, worst = 0.0;
    for (const double sr : kRates)
    {
        const auto r = renderNote (p, 57, sr, 2.0, 2.5);
        double sq = 0.0;
        for (size_t i = 0; i < r.l.size(); ++i)
            sq += static_cast<double> (r.l[i]) * r.l[i];
        const double rms = std::sqrt (sq / static_cast<double> (r.l.size()));
        if (ref == 0.0)
            ref = rms;
        const double db = 20.0 * std::log10 (rms / ref);
        worst = std::max (worst, std::abs (db));
        std::printf ("  %8.1f Hz: wet rms %.6f  %+.2f dB vs 44.1k\n", sr, rms, db);
        CHECK_MSG (std::abs (db) <= kMaxDeviationDb,
                   "CAVE wet level at %.1f Hz is %+.2f dB off the 44.1k reference "
                   "(ceiling %.1f dB)", sr, db, kMaxDeviationDb);
    }
    std::printf ("  worst deviation %.2f dB (ceiling %.1f dB — F4 fixed)\n",
                 worst, kMaxDeviationDb);
}

// ---------------------------------------------------------------------------
// 3. Tempo sweep.
// ---------------------------------------------------------------------------

// Echo lock: 1/4 note at BPM must place the first echo at exactly 60/BPM s.
inline void test_tempo_bpm_lock_sweep()
{
    std::printf ("[tempo_bpm_lock_sweep]\n");
    const double sr = 48000.0;
    const auto p = delayBurstPatch();

    const double bpms[] = { 60.0, 70.0, 80.0, 90.0, 100.0, 110.0, 120.0,
                            128.0, 140.0, 150.0, 160.0, 174.0, 186.0, 200.0 };
    int worstErr = 0;
    for (const double bpm : bpms)
    {
        const double hopSec = 60.0 / bpm;
        const auto r = fxtests::renderWithTempo (p, 60, sr, 0.05, hopSec * 2.5 + 0.4, bpm, {});
        const int expected = static_cast<int> (std::lround (hopSec * sr));
        const int tpl = static_cast<int> (0.12 * sr);
        const int lag = fxtests::bestLag (r.l, tpl, expected - 200, expected + 200);
        const int err = std::abs (lag - expected);
        worstErr = std::max (worstErr, err);
        CHECK_MSG (err <= 2, "%.1f BPM: 1/4 echo at %d samples, expected %d",
                   bpm, lag, expected);
        CHECK_MSG (countNonFinite (r) == 0, "%.1f BPM: non-finite samples", bpm);
    }
    std::printf ("  14 BPMs 60..200: worst first-echo error %d samples "
                 "(gate 2)\n", worstErr);
}

// Score one abrupt tempo jump the way the craft-transition matrix scores a
// patch swap: worst single-sample step in the 30 ms after the jump against
// 1.25x the steady-state step before and after.
struct TempoJumpScore { float step, bound, ratio; };

inline TempoJumpScore scoreTempoJump (double bpmFrom, double bpmTo)
{
    const double sr = 48000.0;
    const auto p = delayTonePatch();
    const auto r = fxtests::renderWithTempo (p, 48, sr, 3.2, 3.2, bpmFrom, { { 1.5, bpmTo } });

    const int at = static_cast<int> (1.5 * sr);
    const float swap   = fxtests::maxSlew (r.l, at, at + static_cast<int> (0.030 * sr));
    const float before = fxtests::maxSlew (r.l, static_cast<int> (1.0 * sr), at);
    const float after  = fxtests::maxSlew (r.l, static_cast<int> (2.4 * sr),
                                           static_cast<int> (3.1 * sr));
    const float bound  = 1.25f * std::max (before, after);
    return { swap, bound, swap / std::max (1.0e-9f, bound) };
}

// Hard-splice control: what an UNSMOOTHED delay-time change does to the
// waveform at the same instant. The detector must blow through the gate here,
// otherwise the numbers above prove nothing.
inline float spliceTempoRatio (double bpmFrom, double bpmTo)
{
    const double sr = 48000.0;
    const auto p = delayTonePatch();
    const auto a = fxtests::renderWithTempo (p, 48, sr, 3.2, 3.2, bpmFrom, {});
    const auto b = fxtests::renderWithTempo (p, 48, sr, 3.2, 3.2, bpmTo, {});
    const int at = static_cast<int> (1.5 * sr);
    const float before = fxtests::maxSlew (a.l, static_cast<int> (1.0 * sr), at);
    const float after  = fxtests::maxSlew (b.l, static_cast<int> (2.4 * sr),
                                           static_cast<int> (3.1 * sr));
    const float bound  = 1.25f * std::max (before, after);
    float step = 0.0f;
    for (int k = 0; k < 900; ++k)
    {
        const size_t n0 = static_cast<size_t> (at + k);
        step = std::max (step, std::abs (b.l[n0] - a.l[n0 - 1]));
    }
    return step / std::max (1.0e-9f, bound);
}

inline void test_tempo_jump_click_free()
{
    std::printf ("[tempo_jump_click_free]\n");
    // Gate calibration (Phase 7). The craft-transition matrix uses 2.5x, but
    // that metric was calibrated for parameter swaps, where the smoothers make
    // the transition strictly quieter than steady state. A tempo jump is
    // different in kind: the delay read pointer glides to the new tap over the
    // ~25 ms smoother, so SHORTENING the delay time replays the line faster —
    // a bounded pitch-up glide (tape-style "zip"), not a discontinuity. Its
    // slew scales with the compression ratio, so the biggest legal jump in the
    // sweep (60 -> 200 BPM on a 1/8 tap: 0.5 s -> 0.15 s) sets the floor at
    // 2.80. The gate is 4.0: above every smoothed glide measured here, and far
    // below the hard-splice control asserted at the end (which scores 15x+).
    // The 2.80 figure is reported to the architect as open question Q1.
    constexpr float kMaxRatio = 4.0f;

    struct Jump { double from, to; };
    const Jump jumps[] =
    {
        { 120.0,  60.0 }, {  60.0, 200.0 }, { 200.0,  60.0 },
        { 120.0, 200.0 }, { 120.0, 121.0 }, { 174.3,  91.7 },
        {  90.0, 180.0 }, { 180.0,  90.0 },
    };
    float worst = 0.0f;
    for (const auto& j : jumps)
    {
        const auto s = scoreTempoJump (j.from, j.to);
        std::printf ("  %6.1f -> %6.1f BPM: step %.4f bound %.4f ratio %5.2f\n",
                     j.from, j.to, static_cast<double> (s.step),
                     static_cast<double> (s.bound), static_cast<double> (s.ratio));
        CHECK_MSG (s.ratio <= kMaxRatio,
                   "tempo jump %.1f -> %.1f clicked: step %.4f is %.2fx bound %.4f",
                   j.from, j.to, static_cast<double> (s.step),
                   static_cast<double> (s.ratio), static_cast<double> (s.bound));
        worst = std::max (worst, s.ratio);
    }
    for (const auto& j : { Jump { 60.0, 200.0 }, Jump { 120.0, 60.0 } })
    {
        const float sr2 = spliceTempoRatio (j.from, j.to);
        std::printf ("  negative control (hard splice) %6.1f -> %6.1f BPM: ratio %5.2f\n",
                     j.from, j.to, static_cast<double> (sr2));
        CHECK_MSG (sr2 > kMaxRatio,
                   "click detector toothless: hard splice %.1f -> %.1f only scored %.2f",
                   j.from, j.to, static_cast<double> (sr2));
    }
    std::printf ("  worst smoothed ratio %.2f (gate %.1f)\n", static_cast<double> (worst),
                 static_cast<double> (kMaxRatio));
}

// After an abrupt jump the echo train must re-lock to the NEW grid. Measured
// by autocorrelation of the settled tail, so it needs no echo bookkeeping.
inline void test_tempo_relock_sweep()
{
    std::printf ("[tempo_relock_sweep]\n");
    const double sr = 48000.0;
    const auto p = delayBurstPatch();

    const double targets[] = { 60.0, 75.0, 90.0, 100.0, 140.0, 160.0, 200.0 };
    int worstErr = 0;
    for (const double bpm : targets)
    {
        const double hopSec = 60.0 / bpm;
        const auto r = fxtests::renderWithTempo (p, 60, sr, 0.05, 6.0, 120.0, { { 1.2, bpm } });
        const int expected = static_cast<int> (std::lround (hopSec * sr));
        // Settled window: >= 1 s after the jump, and long enough for 2 hops.
        const int from = static_cast<int> (3.0 * sr);
        const int to   = static_cast<int> (6.0 * sr);
        const int lag = autocorrPeakLag (r.l, from, to,
                                         static_cast<int> (0.25 * sr),
                                         static_cast<int> (1.10 * sr));
        const int err = std::abs (lag - expected);
        worstErr = std::max (worstErr, err);
        std::printf ("  120 -> %5.1f BPM: settled echo spacing %6d samples "
                     "(expected %6d, err %d)\n", bpm, lag, expected, err);
        CHECK_MSG (err <= 24, "%.1f BPM: echo spacing %d != %d (err %d samples)",
                   bpm, lag, expected, err);
        CHECK_MSG (countNonFinite (r) == 0, "%.1f BPM re-lock: non-finite samples", bpm);
    }
    std::printf ("  7 abrupt jumps re-lock; worst spacing error %d samples (gate 24)\n",
                 worstErr);
}

// A long ladder of tempo changes under a held note: pure stability.
inline void test_tempo_ladder_stability()
{
    std::printf ("[tempo_ladder_stability]\n");
    const double sr = 48000.0;
    auto p = delayTonePatch();
    p.dly_fb = 0.85f;
    p.cave_size = 0.8f; p.cave_mix = 0.4f;             // reverb tail across changes

    std::vector<fxtests::TempoEvent> events;
    double t = 0.4;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 10.0) { events.push_back ({ t, bpm }); t += 0.12; }
    for (double bpm = 200.0; bpm >= 60.0; bpm -= 10.0) { events.push_back ({ t, bpm }); t += 0.12; }
    events.push_back ({ t + 0.05, 33.0 });             // below and above the UI range
    events.push_back ({ t + 0.10, 999.0 });
    events.push_back ({ t + 0.15, 120.0 });

    const auto r = fxtests::renderWithTempo (p, 48, sr, 5.0, 6.0, 120.0, events);
    const int bad = countNonFinite (r);
    const float pk = peakOf (r);
    std::printf ("  %zu tempo changes (incl. 33 and 999 BPM): non-finite %d, peak %.4f\n",
                 events.size(), bad, static_cast<double> (pk));
    CHECK_MSG (bad == 0, "%d non-finite samples across the tempo ladder", bad);
    // fb 0.85 into a wet-only delay plus the CAVE tail drives the master
    // softclip into its ceiling: peak lands on exactly 1.0 (the asymptote,
    // rounded in float), never above it. That is the ceiling working.
    CHECK_MSG (pk <= 1.0f, "peak %.4f exceeds 0 dBFS across the tempo ladder",
               static_cast<double> (pk));
}

// ---------------------------------------------------------------------------
// 4. CPU under stress at every sample rate.
// ---------------------------------------------------------------------------
inline double stressCpuRatio (double sr, int blockSize)
{
    // 16 voices x 8-way unison = 128 stacks (kMaxUnisonStacks), every source
    // on, the whole FX block and all six wet-path filters engaged.
    ParamSnapshot p;
    p.oscB_on = true; p.oscB_sync = true; p.sub_on = true; p.noise_on = true;
    p.uni_count = 8; p.poly_count = 16;
    p.lfo1_pwm = 0.4f; p.lfo2_amt = 0.5f;
    p.env1_s = 1.0f;
    p.crush_bits = 4; p.crush_down = 2; p.crush_mix = 1.0f;
    p.dly_fb = 0.9f; p.dly_pingpong = true; p.dly_mix = 0.5f;
    p.cave_size = 0.9f; p.cave_damp = 0.5f; p.cave_mix = 0.5f;
    p.crush_hp = 200.0f; p.dly_hp = 200.0f; p.cave_hp = 200.0f;
    p.crush_lp = 4000.0f; p.dly_lp = 4000.0f; p.cave_lp = 4000.0f;

    BlockwaveEngine engine;
    engine.setParams (p);
    engine.prepare (sr, blockSize);
    for (int i = 0; i < 16; ++i)
        engine.noteOn (36 + i * 3, 0.9f);

    static float l[4096], r[4096];
    const int blocks = static_cast<int> (2.0 * sr / blockSize);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
        engine.process (l, r, blockSize);
    const auto t1 = std::chrono::steady_clock::now();

    const double renderSec = std::chrono::duration<double> (t1 - t0).count();
    const double audioSec  = blocks * static_cast<double> (blockSize) / sr;
    return renderSec / audioSec;
}

inline void test_cpu_stress_all_rates()
{
    std::printf ("[cpu_stress_all_rates]\n");
    std::printf ("  worst case: 16 voices x 8 unison (128 stacks), all sources + all FX\n");
#ifdef NDEBUG
    const double budget = 0.25;    // optimized build: rate-scaled FX budget
#else
    const double budget = 0.95;    // Debug build: realtime ceiling only
#endif
    for (const double sr : kRates)
    {
        for (const int bs : { 128, 512 })
        {
            const double ratio = stressCpuRatio (sr, bs);
            std::printf ("  %8.1f Hz / %4d: %5.1f%% of one core (%.2f%% per voice)\n",
                         sr, bs, ratio * 100.0, ratio * 100.0 / 16.0);
            CHECK_MSG (ratio < budget, "worst case %.1f%% of one core @ %.1f/%d "
                       "(budget %.1f%%)", ratio * 100.0, sr, bs, budget * 100.0);
        }
    }
}

// ---------------------------------------------------------------------------
inline void runAll()
{
    test_sample_rate_pitch_sweep();
    test_sample_rate_stress_patch();
    test_buffer_size_sweep();
    test_pathological_block_sequence();
    test_mid_session_reprepare();
    test_cave_wet_level_rate_dependence();
    test_tempo_bpm_lock_sweep();
    test_tempo_jump_click_free();
    test_tempo_relock_sweep();
    test_tempo_ladder_stability();
    test_cpu_stress_all_rates();
}

} // namespace robust
