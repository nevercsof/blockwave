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

// Phase-5 FX block tests. Included by TestMain.cpp (after kTestDir) so it
// compiles inside the same strict-warning TU as the rest of the DSP suite.
//
//  - bypass null: any FX settings with mix 0 leave the engine output
//    bit-identical (the goldens double as the default-params bypass proof)
//  - crush: quantization grid matches the bit depth; hold length matches the
//    downsample factor on the absolute sample grid
//  - delay: echo lands exactly on the synced division at the host tempo; a
//    mid-render tempo change re-locks the echo spacing without a click
//  - ping-pong: first echo left, second right
//  - cave: tail survives note-off/allNotesOff and decays monotonically-ish;
//    size scales the RT60; damp makes highs decay faster than mids

#include <cstring>

namespace fxtests
{

using namespace blockwave;
using namespace testutil;

// ---------------------------------------------------------------------------
// Engine render with scripted tempo changes (timeSec -> bpm).
struct TempoEvent { double timeSec; double bpm; };

inline Rendered renderWithTempo (const ParamSnapshot& p, int note, double sr,
                                 double noteSec, double totalSec, double bpm0,
                                 const std::vector<TempoEvent>& tempoEvents,
                                 double allNotesOffSec = -1.0, int blockSize = 512)
{
    BlockwaveEngine engine;
    engine.setParams (p);
    engine.setTempo (bpm0);
    engine.prepare (sr, blockSize);

    const int total = static_cast<int> (totalSec * sr);
    const int offAt = static_cast<int> (noteSec * sr);
    const int panicAt = allNotesOffSec >= 0.0 ? static_cast<int> (allNotesOffSec * sr) : -1;
    Rendered out;
    out.l.assign (static_cast<size_t> (total), 0.0f);
    out.r.assign (static_cast<size_t> (total), 0.0f);

    engine.noteOn (note, 1.0f);
    size_t tIdx = 0;
    bool offSent = false, panicSent = false;
    int pos = 0;
    while (pos < total)
    {
        if (! offSent && pos >= offAt) { engine.noteOff (note); offSent = true; }
        if (panicAt >= 0 && ! panicSent && pos >= panicAt)
        {
            engine.allNotesOff();
            panicSent = true;
        }
        while (tIdx < tempoEvents.size()
               && static_cast<int> (tempoEvents[tIdx].timeSec * sr) <= pos)
        {
            engine.setTempo (tempoEvents[tIdx].bpm);
            ++tIdx;
        }
        int n = std::min (blockSize, total - pos);
        if (! offSent && pos + n > offAt) n = offAt - pos;
        if (n <= 0) n = 1;
        engine.process (out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

// Best lag of template x[0..tplLen) inside x over [lagMin, lagMax].
inline int bestLag (const std::vector<float>& x, int tplLen, int lagMin, int lagMax)
{
    double best = -1.0e30;
    int bestL = lagMin;
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double c = 0.0;
        const int n = std::min (tplLen, static_cast<int> (x.size()) - lag);
        for (int i = 0; i < n; ++i)
            c += static_cast<double> (x[static_cast<size_t> (i)])
               * x[static_cast<size_t> (i + lag)];
        if (c > best) { best = c; bestL = lag; }
    }
    return bestL;
}

inline double windowRms (const std::vector<float>& x, int from, int to)
{
    double s = 0.0;
    const int a = std::max (0, from);
    const int b = std::min (static_cast<int> (x.size()), to);
    for (int i = a; i < b; ++i)
        s += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];
    return b > a ? std::sqrt (s / (b - a)) : 0.0;
}

// RT60 estimate: least-squares slope of tail RMS in dB over 200 ms windows.
inline double measureT60 (const std::vector<float>& x, double sr, double t0, double t1)
{
    const int win = static_cast<int> (0.2 * sr);
    std::vector<double> ts, dbs;
    for (int start = static_cast<int> (t0 * sr);
         start + win <= static_cast<int> (t1 * sr); start += win)
    {
        const double rms = windowRms (x, start, start + win);
        ts.push_back (start / sr);
        dbs.push_back (20.0 * std::log10 (rms + 1.0e-12));
    }
    const size_t n = ts.size();
    if (n < 3)
        return 0.0;
    double mt = 0.0, md = 0.0;
    for (size_t i = 0; i < n; ++i) { mt += ts[i]; md += dbs[i]; }
    mt /= static_cast<double> (n);
    md /= static_cast<double> (n);
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        num += (ts[i] - mt) * (dbs[i] - md);
        den += (ts[i] - mt) * (ts[i] - mt);
    }
    const double slope = num / den;                     // dB per second (< 0)
    return slope < -1.0e-3 ? -60.0 / slope : 1.0e9;
}

// Linear band power from a spectrumDb result.
inline double bandPower (const std::vector<double>& db, double binHz,
                         double loHz, double hiHz)
{
    double p = 0.0;
    for (size_t i = 1; i < db.size(); ++i)
    {
        const double hz = static_cast<double> (i) * binHz;
        if (hz >= loHz && hz <= hiHz)
            p += std::pow (10.0, db[i] / 10.0);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Bypass null: aggressive FX settings with every mix at 0 must not change one
// bit of the output (spec allows <= -120 dB; the design delivers exact zero).
inline void test_fx_bypass_null()
{
    std::printf ("[fx_bypass_null]\n");
    ParamSnapshot base;
    base.oscB_on = true; base.oscB_sync = true; base.oscB_semi = 7;
    base.sub_on = true;  base.noise_on = true;
    base.filt_cutoff = 3000.0f; base.filt_res = 0.3f;

    const auto ref = renderNote (base, 48, 48000.0, 0.4, 0.7);

    struct Variant { const char* name; void (*apply) (ParamSnapshot&); };
    const Variant variants[] =
    {
        { "crush", [] (ParamSnapshot& p) { p.crush_bits = 2; p.crush_down = 32; p.crush_mix = 0.0f; } },
        { "delay", [] (ParamSnapshot& p) { p.dly_time = 10; p.dly_fb = 0.9f; p.dly_pingpong = false; p.dly_mix = 0.0f; } },
        { "cave",  [] (ParamSnapshot& p) { p.cave_size = 1.0f; p.cave_damp = 0.0f; p.cave_mix = 0.0f; } },
        { "all",   [] (ParamSnapshot& p) { p.crush_bits = 1; p.crush_down = 64; p.dly_fb = 0.9f;
                                           p.cave_size = 1.0f; p.crush_mix = 0.0f;
                                           p.dly_mix = 0.0f; p.cave_mix = 0.0f; } },
        // Addendum params cranked with every mix at 0: the wet-path HP must
        // never leak into the dry signal.
        { "hp",    [] (ParamSnapshot& p) { p.crush_hp = 2000.0f; p.dly_hp = 2000.0f;
                                           p.cave_hp = 2000.0f; p.crush_mix = 0.0f;
                                           p.dly_mix = 0.0f; p.cave_mix = 0.0f; } },
    };
    for (const auto& v : variants)
    {
        ParamSnapshot p = base;
        v.apply (p);
        const auto r = renderNote (p, 48, 48000.0, 0.4, 0.7);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < ref.l.size(); ++i)
            maxDiff = std::max ({ maxDiff, std::abs (ref.l[i] - r.l[i]),
                                  std::abs (ref.r[i] - r.r[i]) });
        std::printf ("  %-5s at mix 0: max |diff| = %.3g\n", v.name,
                     static_cast<double> (maxDiff));
        CHECK_MSG (maxDiff == 0.0f, "%s not bit-transparent at mix 0 (diff %.3g)",
                   v.name, static_cast<double> (maxDiff));
    }
}

// ---------------------------------------------------------------------------
// Crush, unit level on the FxChain: quantization grid and hold length.
inline void test_crush_quantize_and_hold()
{
    std::printf ("[crush_quantize_and_hold]\n");
    const double sr = 48000.0;

    // Quantization grid: for bits b every wet output must sit on k / 2^(b-1).
    for (const int bits : { 1, 2, 3, 5, 8 })
    {
        ParamSnapshot p;
        p.crush_mix = 1.0f;
        p.crush_bits = bits;
        p.crush_down = 1;
        FxChain fx;
        fx.prepare (sr, p, 120.0);

        const float q = static_cast<float> (1 << (bits - 1));
        int offGrid = 0;
        float phase = 0.0f;
        for (int block = 0; block < 200; ++block)
        {
            float l[16], r[16];
            for (int i = 0; i < 16; ++i)
            {
                l[i] = r[i] = 0.9f * std::sin (phase);
                phase += 0.031f;
            }
            fx.process (l, r, 16, p, 120.0);
            for (int i = 0; i < 16; ++i)
            {
                const float steps = l[i] * q;
                if (std::abs (steps - std::round (steps)) > 1.0e-3f)
                    ++offGrid;
            }
        }
        std::printf ("  bits=%d: %d/3200 samples off the quantization grid\n", bits, offGrid);
        CHECK_MSG (offGrid == 0, "bits=%d: %d samples off grid", bits, offGrid);
    }

    // Hold length: with down = 8 the output may only change every 8th sample,
    // phase-locked to the absolute sample counter.
    {
        ParamSnapshot p;
        p.crush_mix = 1.0f;
        p.crush_bits = 16;
        p.crush_down = 8;
        FxChain fx;
        fx.prepare (sr, p, 120.0);

        std::vector<float> out;
        float phase = 0.0f;
        for (int block = 0; block < 100; ++block)
        {
            float l[16], r[16];
            for (int i = 0; i < 16; ++i)
            {
                l[i] = r[i] = 0.8f * std::sin (phase);
                phase += 0.07f;
            }
            fx.process (l, r, 16, p, 120.0);
            out.insert (out.end(), l, l + 16);
        }
        int badChanges = 0, goodChanges = 0;
        for (size_t i = 1; i < out.size(); ++i)
        {
            if (std::abs (out[i] - out[i - 1]) > 1.0e-5f)
            {
                if (i % 8 == 0) ++goodChanges;
                else            ++badChanges;
            }
        }
        std::printf ("  down=8: %d changes on the 8-sample grid, %d off it\n",
                     goodChanges, badChanges);
        CHECK_MSG (badChanges == 0, "down=8: %d output changes off the hold grid", badChanges);
        CHECK_MSG (goodChanges > 100, "down=8: hold output barely changes (%d)", goodChanges);
    }
}

// ---------------------------------------------------------------------------
// Delay: exact tempo lock at 120 BPM (1/4 -> 0.5 s), then a mid-render tempo
// change to 90 BPM re-locks the echo spacing to 2/3 s.
inline void test_delay_tempo_lock()
{
    std::printf ("[delay_tempo_lock]\n");
    const double sr = 48000.0;

    ParamSnapshot p;
    p.env1_a = 0.0f; p.env1_d = 0.03f; p.env1_s = 0.0f; p.env1_r = 0.01f;
    p.dly_time = 4;                                   // "1/4"
    p.dly_fb = 0.5f;
    p.dly_mix = 0.5f;
    p.dly_pingpong = false;

    // Burst at t=0, echoes at 0.5 s and 1.0 s; tempo change at 1.2 s.
    const auto r = renderWithTempo (p, 60, sr, 0.05, 3.2, 120.0,
                                    { { 1.2, 90.0 } });
    writeWav16 (kTestDir + "/reports/fx_delay_tempo_change.wav", r.l, r.r,
                static_cast<int> (sr));

    const int tpl = static_cast<int> (0.12 * sr);     // burst template
    const int lag1 = bestLag (r.l, tpl, 23800, 24200);
    std::printf ("  echo 1 lag = %d samples (expect 24000 = 0.5 s @ 120 BPM)\n", lag1);
    CHECK_MSG (std::abs (lag1 - 24000) <= 2, "1/4 @ 120 BPM echo at %d samples", lag1);

    // Echo 3 replays the 1.0 s echo after the new 90 BPM delay has (almost)
    // settled: ~1.0 s + 32000 samples, small smoothing shortfall allowed.
    const int lag3 = bestLag (r.l, tpl, 78500, 81500);
    std::printf ("  echo 3 lag = %d samples (expect ~79900 after 90 BPM change)\n", lag3);
    CHECK_MSG (std::abs (lag3 - 79911) <= 300, "post-change echo at %d samples", lag3);

    // Echo 4 spacing must be the fully settled 90 BPM quarter = 32000 samples.
    const int lag4 = bestLag (r.l, tpl, lag3 + 31500, lag3 + 32500);
    const int spacing = lag4 - lag3;
    std::printf ("  echo 4 spacing = %d samples (expect 32000 = 2/3 s @ 90 BPM)\n", spacing);
    CHECK_MSG (std::abs (spacing - 32000) <= 50,
               "settled echo spacing %d != 32000 samples", spacing);
}

// ---------------------------------------------------------------------------
// Tempo change is click-free: wet-only sustained tone through the delay; the
// smoothed delay-line read makes the transition a brief pitch glide whose
// slew never exceeds steady-state playback. A hard splice (what an unsmoothed
// delay jump does) must trip the same detector.
inline void test_delay_tempo_click_free()
{
    std::printf ("[delay_tempo_click_free]\n");
    const double sr = 48000.0;

    ParamSnapshot p;
    p.env1_a = 0.005f; p.env1_s = 1.0f;
    p.filt_cutoff = 900.0f;                            // smooth waveform, bounded slew
    p.dly_time = 6;                                    // "1/8": 0.25 s @ 120 BPM
    p.dly_fb = 0.5f;
    p.dly_mix = 1.0f;                                  // wet only
    p.dly_pingpong = false;

    const auto r = renderWithTempo (p, 48, sr, 3.0, 3.0, 120.0, { { 1.5, 90.0 } });

    const auto maxSlew = [&r] (double t0, double t1, double srr)
    {
        float m = 0.0f;
        for (int i = static_cast<int> (t0 * srr) + 1; i < static_cast<int> (t1 * srr); ++i)
            m = std::max (m, std::abs (r.l[static_cast<size_t> (i)]
                                     - r.l[static_cast<size_t> (i - 1)]));
        return m;
    };
    const float steady1 = maxSlew (0.9, 1.45, sr);
    const float steady2 = maxSlew (2.5, 2.95, sr);
    const float trans   = maxSlew (1.5, 2.0, sr);
    const float bound   = 1.3f * std::max (steady1, steady2);
    std::printf ("  slew: steady %.4f / %.4f, transition %.4f, bound %.4f\n",
                 static_cast<double> (steady1), static_cast<double> (steady2),
                 static_cast<double> (trans), static_cast<double> (bound));
    CHECK_MSG (trans <= bound, "tempo change clicked: slew %.4f > bound %.4f",
               static_cast<double> (trans), static_cast<double> (bound));

    // Negative control: splicing two steady renders at the change point is
    // what an unsmoothed jump sounds like — the detector must catch it.
    const auto rA = renderWithTempo (p, 48, sr, 3.0, 3.0, 120.0, {});
    const auto rB = renderWithTempo (p, 48, sr, 3.0, 3.0, 90.0, {});
    float spliceSlew = 0.0f;
    for (int k = 0; k < 10; ++k)
    {
        const size_t n0 = static_cast<size_t> (1.5 * sr) + static_cast<size_t> (k * 977);
        spliceSlew = std::max (spliceSlew, std::abs (rB.l[n0] - rA.l[n0 - 1]));
    }
    std::printf ("  negative control (hard splice) slew = %.4f\n",
                 static_cast<double> (spliceSlew));
    CHECK_MSG (spliceSlew > bound, "click detector too lax: splice %.4f under bound %.4f",
               static_cast<double> (spliceSlew), static_cast<double> (bound));
}

// ---------------------------------------------------------------------------
// Ping-pong: first echo on the left only, second on the right only.
inline void test_delay_pingpong()
{
    std::printf ("[delay_pingpong]\n");
    const double sr = 48000.0;

    ParamSnapshot p;
    p.env1_a = 0.0f; p.env1_d = 0.03f; p.env1_s = 0.0f; p.env1_r = 0.01f;
    p.dly_time = 4;                                    // 0.5 s @ 120 BPM
    p.dly_fb = 0.7f;
    p.dly_mix = 1.0f;                                  // wet only
    p.dly_pingpong = true;

    const auto r = renderWithTempo (p, 60, sr, 0.05, 1.4, 120.0, {});
    const int w1a = static_cast<int> (0.49 * sr), w1b = static_cast<int> (0.62 * sr);
    const int w2a = static_cast<int> (0.99 * sr), w2b = static_cast<int> (1.12 * sr);
    const double e1L = windowRms (r.l, w1a, w1b), e1R = windowRms (r.r, w1a, w1b);
    const double e2L = windowRms (r.l, w2a, w2b), e2R = windowRms (r.r, w2a, w2b);
    std::printf ("  echo1 L/R rms = %.5f / %.5f, echo2 L/R rms = %.5f / %.5f\n",
                 e1L, e1R, e2L, e2R);
    CHECK_MSG (e1L > 20.0 * e1R, "echo 1 not left-only (L %.5f vs R %.5f)", e1L, e1R);
    CHECK_MSG (e2R > 20.0 * e2L, "echo 2 not right-only (L %.5f vs R %.5f)", e2L, e2R);
}

// ---------------------------------------------------------------------------
// CAVE tail: survives note-off AND allNotesOff (transport-stop panic), decays
// monotonically-ish, and cave_size scales the measured RT60.
inline void test_cave_tail()
{
    std::printf ("[cave_tail]\n");
    const double sr = 48000.0;

    ParamSnapshot p;
    p.env1_a = 0.005f; p.env1_s = 1.0f; p.env1_r = 0.05f;
    p.cave_mix = 1.0f;                                 // wet only
    p.cave_size = 0.7f;
    p.cave_damp = 0.5f;

    // Note off at 0.5 s, allNotesOff (panic) at 0.7 s — the tail must ring on.
    const auto r = renderWithTempo (p, 45, sr, 0.5, 8.0, 120.0, {}, 0.7);
    writeWav16 (kTestDir + "/reports/fx_cave_tail.wav", r.l, r.r, static_cast<int> (sr));

    int nonFinite = 0;
    for (size_t i = 0; i < r.l.size(); ++i)
        if (! std::isfinite (r.l[i]) || ! std::isfinite (r.r[i]))
            ++nonFinite;
    CHECK_MSG (nonFinite == 0, "%d non-finite samples in reverb render", nonFinite);

    const int win = static_cast<int> (0.25 * sr);
    std::vector<double> rms;
    for (int start = static_cast<int> (1.0 * sr);
         start + win <= static_cast<int> (7.5 * sr); start += win)
        rms.push_back (windowRms (r.l, start, start + win));

    CHECK_MSG (rms.front() > 1.0e-4, "no tail after note-off (rms %.3g)", rms.front());
    // Monotonically-ish: FDN modal beating may bump the odd 250 ms window, so
    // allow up to 2 windows exceeding the previous one by > 15%.
    int bumps = 0;
    for (size_t i = 1; i < rms.size(); ++i)
        if (rms[i] > rms[i - 1] * 1.15)
            ++bumps;
    std::printf ("  tail windows: first %.5f, last %.6f, growth violations %d/%zu\n",
                 rms.front(), rms.back(), bumps, rms.size() - 1);
    CHECK_MSG (bumps <= 2, "tail not monotonically-ish decaying (%d growth windows)", bumps);
    CHECK_MSG (rms.back() < 0.05 * rms.front(),
               "tail barely decayed: %.3g -> %.3g", rms.front(), rms.back());

    // Size scaling: measured RT60 must grow strongly with cave_size.
    ParamSnapshot pBig = p;  pBig.cave_size = 0.9f; pBig.cave_damp = 0.4f;
    ParamSnapshot pSml = p;  pSml.cave_size = 0.2f; pSml.cave_damp = 0.4f;
    const auto rBig = renderWithTempo (pBig, 45, sr, 0.5, 8.0, 120.0, {});
    const auto rSml = renderWithTempo (pSml, 45, sr, 0.5, 4.0, 120.0, {});
    const double t60Big = measureT60 (rBig.l, sr, 1.0, 7.5);
    const double t60Sml = measureT60 (rSml.l, sr, 0.8, 2.2);
    std::printf ("  RT60-ish: size 0.9 -> %.2f s, size 0.2 -> %.2f s\n", t60Big, t60Sml);
    CHECK_MSG (t60Big > 1.8 * t60Sml, "size barely changes decay (%.2f vs %.2f)",
               t60Big, t60Sml);
    CHECK_MSG (t60Big > 3.0 && t60Big < 40.0, "size 0.9 RT60 %.2f s not cavernous", t60Big);
    CHECK_MSG (t60Sml > 0.1 && t60Sml < 3.0, "size 0.2 RT60 %.2f s out of range", t60Sml);
}

// ---------------------------------------------------------------------------
// CAVE damping: with damp up, highs (3-6 kHz) decay faster than mids
// (300-1200 Hz), and faster than they do with damp down.
inline void test_cave_damp_spectral()
{
    std::printf ("[cave_damp_spectral]\n");
    const double sr = 48000.0;
    const size_t fftN = 16384;

    const auto renderDamped = [&] (float damp)
    {
        ParamSnapshot p;
        p.oscA_on = false;
        p.noise_on = true;                             // broadband excitation
        p.noise_level = 0.9f;
        p.env1_a = 0.005f; p.env1_s = 1.0f; p.env1_r = 0.05f;
        p.cave_mix = 1.0f;
        p.cave_size = 0.8f;
        p.cave_damp = damp;
        return renderWithTempo (p, 60, sr, 0.6, 3.6, 120.0, {});
    };

    const auto drops = [&] (const Rendered& r, double& hfDrop, double& midDrop)
    {
        const int early = static_cast<int> (0.8 * sr);
        const int late  = static_cast<int> (2.8 * sr);
        const double binHz = sr / static_cast<double> (fftN);
        const auto dbE = spectrumDb (r.l.data() + early, fftN);
        const auto dbL = spectrumDb (r.l.data() + late, fftN);
        const double hfE  = bandPower (dbE, binHz, 3000.0, 6000.0);
        const double hfL  = bandPower (dbL, binHz, 3000.0, 6000.0);
        const double midE = bandPower (dbE, binHz, 300.0, 1200.0);
        const double midL = bandPower (dbL, binHz, 300.0, 1200.0);
        hfDrop  = 10.0 * std::log10 ((hfE + 1.0e-20) / (hfL + 1.0e-20));
        midDrop = 10.0 * std::log10 ((midE + 1.0e-20) / (midL + 1.0e-20));
    };

    double hfHi = 0.0, midHi = 0.0, hfLo = 0.0, midLo = 0.0;
    drops (renderDamped (0.85f), hfHi, midHi);
    drops (renderDamped (0.10f), hfLo, midLo);
    std::printf ("  damp 0.85: HF drop %.1f dB vs mid drop %.1f dB (2.0 s apart)\n", hfHi, midHi);
    std::printf ("  damp 0.10: HF drop %.1f dB vs mid drop %.1f dB\n", hfLo, midLo);
    CHECK_MSG (hfHi > midHi + 3.0,
               "damp up: highs (%.1f dB) not decaying faster than mids (%.1f dB)", hfHi, midHi);
    CHECK_MSG (hfHi - midHi > (hfLo - midLo) + 2.0,
               "raising damp does not increase relative HF decay (%.1f vs %.1f)",
               hfHi - midHi, hfLo - midLo);
}

// ---------------------------------------------------------------------------
// Frozen-table addendum (cave_hp / dly_hp / crush_hp): at the 20 Hz default
// the HP is a hard bypass, so a render with every FX WET and all three params
// at default must be bit-identical forever. The golden below locks that; the
// one-off pre/post-change equivalence was proven externally with tools/render
// (same preset, pre-change binary vs post-change binary, byte-identical WAVs
// — see the Phase-6 feedback report).
inline void test_fx_hp_default_transparent()
{
    std::printf ("[fx_hp_default_transparent]\n");
    ParamSnapshot p;
    p.oscB_on = true; p.oscB_sync = true; p.oscB_semi = 7;
    p.sub_on = true;  p.noise_on = true;
    p.filt_cutoff = 3000.0f; p.filt_res = 0.3f;
    p.crush_bits = 6; p.crush_down = 3; p.crush_mix = 0.6f;
    p.dly_time = 6; p.dly_fb = 0.5f; p.dly_mix = 0.5f;
    p.cave_size = 0.6f; p.cave_damp = 0.5f; p.cave_mix = 0.5f;
    // cave_hp / dly_hp / crush_hp stay at the 20 Hz default = bypass.

    const auto r = renderNote (p, 45, 48000.0, 0.5, 1.0);

    const std::string f32Path = kTestDir + "/golden/fx_wet_default_48k.f32";
    const std::string wavPath = kTestDir + "/golden/fx_wet_default_48k.wav";
    std::vector<float> golden;
    if (! readF32 (f32Path, golden))
    {
        writeF32 (f32Path, r.l);
        writeWav16 (wavPath, r.l, r.r, 48000);
        std::printf ("  created golden %s — re-run to verify\n", f32Path.c_str());
        return;
    }
    CHECK_MSG (golden.size() == r.l.size(), "golden length %zu != render %zu",
               golden.size(), r.l.size());
    float maxDiff = 0.0f;
    const size_t n = std::min (golden.size(), r.l.size());
    for (size_t i = 0; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (golden[i] - r.l[i]));
    std::printf ("  wet FX @ default HP vs golden: max |diff| = %.3g\n",
                 static_cast<double> (maxDiff));
    CHECK_MSG (maxDiff == 0.0f, "default HP not bit-transparent (diff %.3g)",
               static_cast<double> (maxDiff));
}

// ---------------------------------------------------------------------------
// The audible contract: each *_hp removes sub-100 Hz energy from its FX's WET
// output (measured on an A1 note, fundamental 55 Hz) while leaving the
// effect's mid band essentially untouched — HP the rumble, keep the body.
inline void test_fx_hp_wet_rumble_cut()
{
    std::printf ("[fx_hp_wet_rumble_cut]\n");
    const double sr = 48000.0;
    const size_t fftN = 32768;
    const double binHz = sr / static_cast<double> (fftN);

    struct Case
    {
        const char* name;
        void (*base) (ParamSnapshot&);        // FX wet-only, hp default
        void (*hp) (ParamSnapshot&);          // same + hp engaged
        double t0;                            // spectrum window start (s)
        double minSubDrop, maxMidDrop;        // dB gates
    };
    const Case cases[] =
    {
        { "cave",
          [] (ParamSnapshot& p) { p.cave_mix = 1.0f; p.cave_size = 0.7f;
                                  p.env1_s = 1.0f; p.env1_r = 0.05f; },
          [] (ParamSnapshot& p) { p.cave_hp = 200.0f; },
          1.2, 8.0, 3.0 },
        { "delay",
          [] (ParamSnapshot& p) { p.dly_mix = 1.0f; p.dly_time = 6;   // 1/8
                                  p.dly_fb = 0.5f; p.dly_pingpong = false;
                                  p.env1_s = 1.0f; },
          [] (ParamSnapshot& p) { p.dly_hp = 250.0f; },
          1.0, 8.0, 3.5 },
        { "crush",
          [] (ParamSnapshot& p) { p.crush_mix = 1.0f; p.crush_bits = 16;
                                  p.crush_down = 1; p.env1_s = 1.0f; },
          [] (ParamSnapshot& p) { p.crush_hp = 400.0f; },
          0.6, 12.0, 3.0 },
    };

    for (const auto& c : cases)
    {
        ParamSnapshot pBase;
        c.base (pBase);
        ParamSnapshot pHp = pBase;
        c.hp (pHp);

        const auto rBase = renderNote (pBase, 33, sr, 2.0, 2.2);   // A1, 55 Hz
        const auto rHp   = renderNote (pHp,   33, sr, 2.0, 2.2);

        const int from = static_cast<int> (c.t0 * sr);
        const auto dbBase = spectrumDb (rBase.l.data() + from, fftN);
        const auto dbHp   = spectrumDb (rHp.l.data()   + from, fftN);

        const double subDrop = 10.0 * std::log10 (
            (bandPower (dbBase, binHz, 20.0, 100.0) + 1.0e-20)
          / (bandPower (dbHp,   binHz, 20.0, 100.0) + 1.0e-20));
        const double midDrop = 10.0 * std::log10 (
            (bandPower (dbBase, binHz, 300.0, 1200.0) + 1.0e-20)
          / (bandPower (dbHp,   binHz, 300.0, 1200.0) + 1.0e-20));
        std::printf ("  %-5s hp: sub(20-100 Hz) drop %5.1f dB (need >= %.0f), "
                     "mid(300-1200) drop %4.1f dB (allow <= %.1f)\n",
                     c.name, subDrop, c.minSubDrop, midDrop, c.maxMidDrop);
        CHECK_MSG (subDrop >= c.minSubDrop,
                   "%s hp cut only %.1f dB of sub energy", c.name, subDrop);
        CHECK_MSG (midDrop <= c.maxMidDrop,
                   "%s hp ate %.1f dB of the effect's mid band", c.name, midDrop);
    }
}

} // namespace fxtests
