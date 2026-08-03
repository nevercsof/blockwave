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

// Phase-5 FX block: [CRUSH] -> [DELAY] -> [CAVE], applied by the engine to the
// per-voice sum, before master gain + softclip (SPEC signal path).
//
// Real-time contract (CLAUDE.md): all buffers allocated in prepare(); the
// process path allocates nothing, locks nothing, throws nothing. The engine
// calls process() once per control chunk (<= kCtrlSamples, never spanning the
// absolute 16-sample grid), so per-chunk smoothing composes identically for
// every host block size — with static parameters the output is bit-identical
// from 16 to 4096 samples per block, same guarantee as the engine smoothers.
//
// Bypass guarantee: every mix blend is `dry + mix*(wet - dry)`. With mix
// exactly 0 (snapped default) the dry signal passes through bit-exact, so all
// pre-FX golden renders are unchanged.
//
// TEMPO FLOOR (documented limit): the delay buffer is sized for the longest
// division (1/1) at kMinTempoBpm = 20 BPM -> kMaxDelaySeconds = 12 s. Hosts
// running slower than 20 BPM get the delay time clamped to 12 s.
//
// Tempo changes: the delay time in samples follows a ~100 ms one-pole toward
// the new target and the line is read with linear interpolation — a brief
// tape-style pitch glide, always continuous, never a click or buffer crash.
//
// FX always process (cheap, constant cost) so delay/reverb tails keep ringing
// after note-off and after transport stop, and so raising a mix knob meets a
// line that already carries signal. Nothing here listens to transport state,
// therefore nothing can reset a tail on stop.

#include <cmath>
#include <cstdint>
#include <vector>
#include "BlockwaveParams.h"

namespace blockwave
{

constexpr double kMinTempoBpm     = 20.0;   // delay tempo floor, documented above
constexpr double kMaxDelaySeconds = 240.0 / kMinTempoBpm;   // 12 s = 1/1 @ 20 BPM

// Flush denormal-range values to zero in feedback paths (the offline test
// binary runs without FTZ/DAZ; the plugin additionally uses ScopedNoDenormals).
inline float flushDenorm (float x) noexcept
{
    return (x > -1.0e-20f && x < 1.0e-20f) ? 0.0f : x;
}

// dly_time choice index -> note length in whole notes. Order matches the
// FROZEN kDlyTimeChoices table in ParamSpec.h exactly:
// 1/1, 1/2D, 1/2, 1/4D, 1/4, 1/8D, 1/8, 1/16D, 1/16, 1/32D, 1/32.
inline double delayWholeNotes (int choiceIndex) noexcept
{
    constexpr double t[11] = { 1.0, 0.75, 0.5, 0.375, 0.25, 0.1875, 0.125,
                               0.09375, 0.0625, 0.046875, 0.03125 };
    const int i = choiceIndex < 0 ? 0 : (choiceIndex > 10 ? 10 : choiceIndex);
    return t[i];
}

inline double delaySecondsForChoice (int choiceIndex, double bpm) noexcept
{
    const double b = bpm < kMinTempoBpm ? kMinTempoBpm : bpm;
    const double sec = delayWholeNotes (choiceIndex) * 240.0 / b;
    return sec > kMaxDelaySeconds ? kMaxDelaySeconds : sec;
}

// ---------------------------------------------------------------------------
// FX wet-path high-pass (cave_hp / dly_hp / crush_hp, frozen-table addendum).
//
// One-pole TPT high-pass, 6 dB/oct — enough to keep rumble out of reverb and
// delay tails without thinning the effect character. Each FX filters ONLY the
// signal entering it (pre-quantize for CRUSH, the delay-line/FDN input for
// DELAY/CAVE), so the dry path is untouched and recirculating feedback is
// filtered exactly once, never per echo.
//
// BYPASS CONTRACT: the parameter minimum (20 Hz, the default) means OFF —
// the filter is not run at all and its state is cleared, so every render with
// the default value is BIT-IDENTICAL to the pre-addendum engine (all existing
// goldens double as the transparency proof). The FxChain smooths the cutoff
// (~25 ms) and only drops back to bypass once the smoothed value has settled
// at the floor, so engaging/disengaging swaps at a ~20 Hz corner where the
// filter differs from identity by sub-20 Hz content only — click-free.
constexpr float kFxHpBypassHz = 20.5f;   // engaged strictly above this

struct OnePoleHp
{
    float s = 0.0f;                       // TPT integrator state

    void clear() noexcept { s = 0.0f; }

    // G = g / (1 + g), g = tan(pi * fc / sr); computed once per control chunk.
    static float coefficient (float fcHz, float sr) noexcept
    {
        const float g = std::tan (3.14159265f * fcHz / sr);
        return g / (1.0f + g);
    }

    float process (float x, float G) noexcept
    {
        const float v  = (x - s) * G;
        const float lp = v + s;
        s = flushDenorm (lp + v);
        return x - lp;
    }
};

// ---------------------------------------------------------------------------
// CRUSH: sample-rate downsample (hold every crush_down-th input sample) then
// bit-depth quantization of the held value. Stereo, stateless apart from the
// hold register; runs unconditionally (bits=16, down=1 is transparent to
// ~1e-5 and the mix blend guarantees bit-exact bypass at mix 0).
class CrushFx
{
public:
    void reset() noexcept
    {
        holdL = 0.0f; holdR = 0.0f; counter = 0;
        hpL.clear(); hpR.clear();
    }

    // hpOn/hpG: wet-path high-pass on the signal entering the crusher
    // (pre-quantize) — the dry side of the mix blend stays untouched.
    void process (float* l, float* r, int n, int bits, int down, float mix,
                  bool hpOn, float hpG) noexcept
    {
        const int d = down < 1 ? 1 : (down > 64 ? 64 : down);
        const int b = bits < 1 ? 1 : (bits > 16 ? 16 : bits);
        const float q    = static_cast<float> (1 << (b - 1));   // 2^(bits-1) levels/unit
        const float invq = 1.0f / q;
        if (counter >= d)
            counter = 0;                       // down lowered mid-run
        if (! hpOn)
        {
            hpL.clear();                       // bypass = pre-addendum path,
            hpR.clear();                       // bit-exact (see OnePoleHp)
        }
        for (int i = 0; i < n; ++i)
        {
            float inL = l[i], inR = r[i];
            if (hpOn)
            {
                inL = hpL.process (inL, hpG);
                inR = hpR.process (inR, hpG);
            }
            if (counter == 0) { holdL = inL; holdR = inR; }
            if (++counter >= d)
                counter = 0;
            const float wl = std::floor (holdL * q + 0.5f) * invq;
            const float wr = std::floor (holdR * q + 0.5f) * invq;
            l[i] += mix * (wl - l[i]);
            r[i] += mix * (wr - r[i]);
        }
    }

private:
    float holdL = 0.0f, holdR = 0.0f;
    int counter = 0;
    OnePoleHp hpL, hpR;
};

// ---------------------------------------------------------------------------
// DELAY: tempo-synced stereo delay, normal (independent L/R feedback) or
// ping-pong (mono input, cross feedback, echoes alternate L, R, L...).
// The delay time in samples is smoothed by the FxChain and read with linear
// interpolation (see header comment).
class BlockDelay
{
public:
    void prepare (double sampleRate) noexcept
    {
        capacity = static_cast<int> (kMaxDelaySeconds * sampleRate) + 4;
        bufL.assign (static_cast<size_t> (capacity), 0.0f);
        bufR.assign (static_cast<size_t> (capacity), 0.0f);
        w = 0;
    }

    void clear() noexcept
    {
        for (auto& v : bufL) v = 0.0f;
        for (auto& v : bufR) v = 0.0f;
        w = 0;
        hpL.clear();
        hpR.clear();
    }

    int maxDelaySamples() const noexcept { return capacity - 3; }

    // The delay time ramps linearly from delaySamplesStart to
    // delaySamplesEnd across the chunk: during tempo changes the read position
    // moves continuously per sample (tape-style glide, never a per-chunk jump).
    // Static time -> flat ramp -> block-size bit-exactness preserved.
    // hpOn/hpG: wet-path high-pass on the signal entering the delay line —
    // everything in the line (and thus every echo) is filtered exactly once;
    // the dry side of the mix blend stays untouched.
    void process (float* l, float* r, int n, double delaySamplesStart,
                  double delaySamplesEnd, float fb, float mix, bool pingpong,
                  bool hpOn, float hpG) noexcept
    {
        const double dmax = static_cast<double> (capacity - 3);
        const auto clampDs = [dmax] (double v) noexcept
        {
            return v < 1.0 ? 1.0 : (v > dmax ? dmax : v);
        };
        double ds = clampDs (delaySamplesStart);
        const double step = (clampDs (delaySamplesEnd) - ds)
                          / static_cast<double> (n > 0 ? n : 1);

        if (! hpOn)
        {
            hpL.clear();                       // bypass = pre-addendum path,
            hpR.clear();                       // bit-exact (see OnePoleHp)
        }
        for (int i = 0; i < n; ++i)
        {
            ds += step;
            const int   di   = static_cast<int> (ds);
            const float frac = static_cast<float> (ds - di);
            int r0 = w - di;      if (r0 < 0) r0 += capacity;
            int r1 = r0 - 1;      if (r1 < 0) r1 += capacity;
            const auto i0 = static_cast<size_t> (r0);
            const auto i1 = static_cast<size_t> (r1);
            const float wl = bufL[i0] + frac * (bufL[i1] - bufL[i0]);
            const float wr = bufR[i0] + frac * (bufR[i1] - bufR[i0]);

            const float inL = l[i], inR = r[i];
            float fInL = inL, fInR = inR;
            if (hpOn)
            {
                fInL = hpL.process (inL, hpG);
                fInR = hpR.process (inR, hpG);
            }
            const auto iw = static_cast<size_t> (w);
            if (pingpong)
            {
                // Mono in -> L line; each hop L->R->L decays by fb.
                const float mono = 0.5f * (fInL + fInR);
                bufL[iw] = flushDenorm (mono + fb * wr);
                bufR[iw] = flushDenorm (fb * wl);
            }
            else
            {
                bufL[iw] = flushDenorm (fInL + fb * wl);
                bufR[iw] = flushDenorm (fInR + fb * wr);
            }
            l[i] = inL + mix * (wl - inL);
            r[i] = inR + mix * (wr - inR);
            if (++w >= capacity)
                w = 0;
        }
    }

private:
    std::vector<float> bufL, bufR;
    int capacity = 8;
    int w = 0;
    OnePoleHp hpL, hpR;
};

// ---------------------------------------------------------------------------
// CAVE: dark cavernous algorithmic reverb. 8-line FDN with a Householder
// feedback matrix (I - 2/8 * ones — orthogonal, energy preserving), per-line
// one-pole damping lowpass, per-line gain set from an RT60 target, size-scaled
// mutually-detuned loop lengths, an input DC blocker and a size-scaled
// pre-delay whose half-way tap adds early-reflection flavor.
//
//   RT60(size) = 0.3 + 12 * size^2 seconds   (size 0 -> 0.3 s, 0.5 -> 3.3 s,
//                                             1.0 -> 12.3 s: a proper cave)
//   damping    fc = 9 kHz * 2^(-3.2*damp)    (9 kHz -> ~980 Hz; always dark)
class CaveReverb
{
public:
    static constexpr int kLines = 8;

    // Loop lengths at size = 1, milliseconds; pairwise non-rational ratios.
    static const float* baseMs() noexcept
    {
        static constexpr float ms[kLines] = { 43.7f, 57.3f, 68.9f, 79.9f,
                                              93.1f, 107.7f, 122.3f, 139.9f };
        return ms;
    }

    static double rt60ForSize (float size) noexcept
    {
        const double s = size < 0.0f ? 0.0 : (size > 1.0f ? 1.0 : static_cast<double> (size));
        return 0.3 + 12.0 * s * s;
    }

    static float lengthScale (float size) noexcept       // loop-length factor
    {
        const float s = size < 0.0f ? 0.0f : (size > 1.0f ? 1.0f : size);
        return 0.35f + 0.65f * s;
    }

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        lineCap = static_cast<int> (0.150 * sampleRate) + 4;   // >= 139.9 ms
        preCap  = static_cast<int> (0.060 * sampleRate) + 4;   // >= 50 ms
        for (int i = 0; i < kLines; ++i)
            lines[i].assign (static_cast<size_t> (lineCap), 0.0f);
        pre.assign (static_cast<size_t> (preCap), 0.0f);
        clearState();
    }

    void clearState() noexcept
    {
        for (int i = 0; i < kLines; ++i)
        {
            for (auto& v : lines[i]) v = 0.0f;
            wIdx[i] = 0;
            lp[i] = 0.0f;
        }
        for (auto& v : pre) v = 0.0f;
        preW = 0;
        dcX = 0.0f;
        dcY = 0.0f;
        hpIn.clear();
    }

    // Control-rate update (once per chunk): smoothed lengths -> per-line gain.
    void updateGains (const float* smoothedLen, double rt60) noexcept
    {
        for (int i = 0; i < kLines; ++i)
        {
            const double lenSec = static_cast<double> (smoothedLen[i]) / sr;
            g[i] = static_cast<float> (std::pow (10.0, -3.0 * lenSec / rt60));
        }
    }

    // hpOn/hpG: wet-path high-pass on the mono input entering the reverb —
    // the tail carries no rumble; the dry side of the mix blend is untouched.
    void process (float* l, float* r, int n, const float* smoothedLen,
                  float preSamples, float dampCoef, float mix,
                  bool hpOn, float hpG) noexcept
    {
        float pMax = static_cast<float> (preCap - 3);
        const float ps  = preSamples < 1.0f ? 1.0f : (preSamples > pMax ? pMax : preSamples);
        const float eps = ps * 0.5f;

        if (! hpOn)
            hpIn.clear();                      // bypass = pre-addendum path
        for (int i = 0; i < n; ++i)
        {
            // Input: mono sum, high-passed (cave_hp), DC-blocked, into the
            // pre-delay line.
            float x = 0.5f * (l[i] + r[i]);
            if (hpOn)
                x = hpIn.process (x, hpG);
            const float hp = x - dcX + 0.995f * dcY;
            dcX = x;
            dcY = flushDenorm (hp);
            pre[static_cast<size_t> (preW)] = hp;

            const float pIn = readLine (pre.data(), preCap, preW, ps);
            const float er  = readLine (pre.data(), preCap, preW, eps);
            if (++preW >= preCap)
                preW = 0;

            // Read all loops (linear interp at the smoothed lengths).
            float y[kLines];
            float sum = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                y[k] = readLine (lines[k].data(), lineCap, wIdx[k], smoothedLen[k]);
                sum += y[k];
            }
            const float h = 0.25f * sum;                   // 2/N with N = 8

            // Householder feedback -> damping -> gain -> write (+ input).
            const float inj = 0.5f * pIn;
            for (int k = 0; k < kLines; ++k)
            {
                const float v = y[k] - h;
                lp[k] = flushDenorm (lp[k] + dampCoef * (v - lp[k]));
                const float sign = (k & 1) != 0 ? -1.0f : 1.0f;
                lines[k][static_cast<size_t> (wIdx[k])] = g[k] * lp[k] + sign * inj;
                if (++wIdx[k] >= lineCap)
                    wIdx[k] = 0;
            }

            const float wetL = 0.38f * (y[0] - y[2] + y[4] - y[6]) + 0.22f * er;
            const float wetR = 0.38f * (y[1] - y[3] + y[5] - y[7]) + 0.22f * er;
            l[i] += mix * (wetL - l[i]);
            r[i] += mix * (wetR - r[i]);
        }
    }

private:
    static float readLine (const float* buf, int cap, int w, float delaySamples) noexcept
    {
        const int   di   = static_cast<int> (delaySamples);
        const float frac = delaySamples - static_cast<float> (di);
        int r0 = w - di;      if (r0 < 0) r0 += cap;
        int r1 = r0 - 1;      if (r1 < 0) r1 += cap;
        const float a = buf[static_cast<size_t> (r0)];
        const float b = buf[static_cast<size_t> (r1)];
        return a + frac * (b - a);
    }

    double sr = 44100.0;
    std::vector<float> lines[kLines];
    std::vector<float> pre;
    int lineCap = 8, preCap = 8;
    int wIdx[kLines] {};
    int preW = 0;
    float lp[kLines] {};                      // damping filter state
    float g[kLines] {};                       // per-line RT60 gain
    float dcX = 0.0f, dcY = 0.0f;             // input DC blocker
    OnePoleHp hpIn;                           // cave_hp (mono input)
};

// ---------------------------------------------------------------------------
// The full FX block with its parameter smoothing. Owned by BlockwaveEngine;
// process() is called once per control chunk on the absolute 16-sample grid.
class FxChain
{
public:
    void prepare (double sampleRate, const ParamSnapshot& p, double bpm) noexcept
    {
        sr = sampleRate;
        lambdaMix  = -1.0f / (0.025f * static_cast<float> (sampleRate));  // ~25 ms
        lambdaTime = -1.0f / (0.100f * static_cast<float> (sampleRate));  // ~100 ms
        delay.prepare (sampleRate);
        cave.prepare (sampleRate);
        reset (p, bpm);
    }

    // Clears all FX state and snaps every smoother to its target. Called from
    // prepare() / engine reset() only — never from transport events, so tails
    // survive transport stop.
    void reset (const ParamSnapshot& p, double bpm) noexcept
    {
        crush.reset();
        delay.clear();
        cave.clearState();
        sCrushMix = p.crush_mix;
        sDlyMix   = p.dly_mix;
        sDlyFb    = p.dly_fb;
        sCaveMix  = p.cave_mix;
        sCrushHp  = p.crush_hp;
        sDlyHp    = p.dly_hp;
        sCaveHp   = p.cave_hp;
        sDlyTime  = delayTargetSamples (p, bpm);
        const float ls = CaveReverb::lengthScale (p.cave_size);
        for (int i = 0; i < CaveReverb::kLines; ++i)
            sCaveLen[i] = CaveReverb::baseMs()[i] * 0.001f * ls * static_cast<float> (sr);
        sCavePre = preTargetSamples (p.cave_size);
    }

    void process (float* l, float* r, int n, const ParamSnapshot& p, double bpm) noexcept
    {
        // Advance smoothers with the exact per-chunk exponent (bit-identical
        // across block splits for static params, as in BlockwaveEngine).
        const float aMix  = 1.0f - std::exp (lambdaMix  * static_cast<float> (n));
        const float aTime = 1.0f - std::exp (lambdaTime * static_cast<float> (n));
        sCrushMix += aMix * (p.crush_mix - sCrushMix);
        sDlyMix   += aMix * (p.dly_mix   - sDlyMix);
        sDlyFb    += aMix * (p.dly_fb    - sDlyFb);
        sCaveMix  += aMix * (p.cave_mix  - sCaveMix);
        sCrushHp  += aMix * (p.crush_hp  - sCrushHp);
        sDlyHp    += aMix * (p.dly_hp    - sDlyHp);
        sCaveHp   += aMix * (p.cave_hp   - sCaveHp);

        // Wet-path HP engage/bypass (see OnePoleHp header comment): stay
        // engaged while either the target or the smoothed cutoff is above the
        // floor, so a preset jump back to 20 Hz glides down first and only
        // then swaps to the bit-exact bypass path.
        const float fsr = static_cast<float> (sr);
        const bool  crushHpOn = p.crush_hp > kFxHpBypassHz || sCrushHp > kFxHpBypassHz;
        const bool  dlyHpOn   = p.dly_hp   > kFxHpBypassHz || sDlyHp   > kFxHpBypassHz;
        const bool  caveHpOn  = p.cave_hp  > kFxHpBypassHz || sCaveHp  > kFxHpBypassHz;
        const float crushHpG = crushHpOn ? OnePoleHp::coefficient (sCrushHp, fsr) : 0.0f;
        const float dlyHpG   = dlyHpOn   ? OnePoleHp::coefficient (sDlyHp,   fsr) : 0.0f;
        const float caveHpG  = caveHpOn  ? OnePoleHp::coefficient (sCaveHp,  fsr) : 0.0f;
        const double dlyTimePrev = sDlyTime;
        sDlyTime  += static_cast<double> (aTime) * (delayTargetSamples (p, bpm) - sDlyTime);
        const float ls = CaveReverb::lengthScale (p.cave_size);
        for (int i = 0; i < CaveReverb::kLines; ++i)
        {
            const float target = CaveReverb::baseMs()[i] * 0.001f * ls
                               * static_cast<float> (sr);
            sCaveLen[i] += aTime * (target - sCaveLen[i]);
        }
        sCavePre += aTime * (preTargetSamples (p.cave_size) - sCavePre);

        const float damp = p.cave_damp < 0.0f ? 0.0f
                         : (p.cave_damp > 1.0f ? 1.0f : p.cave_damp);
        const float fcHz = 9000.0f * std::exp2 (-3.2f * damp);
        const float dampCoef = 1.0f - std::exp (-6.2831853f * fcHz
                                                / static_cast<float> (sr));
        cave.updateGains (sCaveLen, CaveReverb::rt60ForSize (p.cave_size));

        crush.process (l, r, n, p.crush_bits, p.crush_down, sCrushMix,
                       crushHpOn, crushHpG);
        delay.process (l, r, n, dlyTimePrev, sDlyTime, sDlyFb, sDlyMix,
                       p.dly_pingpong, dlyHpOn, dlyHpG);
        cave.process  (l, r, n, sCaveLen, sCavePre, dampCoef, sCaveMix,
                       caveHpOn, caveHpG);
    }

    // Honest tail estimate for AudioProcessor::getTailLengthSeconds — time for
    // the delay feedback to fall 60 dB plus the reverb RT60, capped at 120 s
    // so pathological settings (0.9 feedback on a 12 s delay) don't stall
    // offline renders forever.
    static double tailSeconds (const ParamSnapshot& p, double bpm) noexcept
    {
        double tail = 0.0;
        if (p.dly_mix > 1.0e-4f)
        {
            const double sec = delaySecondsForChoice (p.dly_time, bpm);
            const double fb  = static_cast<double> (p.dly_fb);
            const double hops = fb > 1.0e-3 ? std::log (1.0e-3) / std::log (fb) : 1.0;
            tail += sec * (hops < 1.0 ? 1.0 : hops);
        }
        if (p.cave_mix > 1.0e-4f)
            tail += CaveReverb::rt60ForSize (p.cave_size);
        return tail > 120.0 ? 120.0 : tail;
    }

private:
    double delayTargetSamples (const ParamSnapshot& p, double bpm) const noexcept
    {
        const double s = delaySecondsForChoice (p.dly_time, bpm) * sr;
        const double dmax = static_cast<double> (delay.maxDelaySamples());
        return s > dmax ? dmax : s;
    }

    float preTargetSamples (float size) const noexcept
    {
        const float s = size < 0.0f ? 0.0f : (size > 1.0f ? 1.0f : size);
        return (0.015f + 0.035f * s) * static_cast<float> (sr);
    }

    double sr = 44100.0;
    float lambdaMix = -0.001f, lambdaTime = -0.001f;

    CrushFx    crush;
    BlockDelay delay;
    CaveReverb cave;

    // Smoothed values (audible params, ~25 ms; time-like params, ~100 ms).
    float  sCrushMix = 0.0f, sDlyMix = 0.0f, sDlyFb = 0.35f, sCaveMix = 0.0f;
    float  sCrushHp = 20.0f, sDlyHp = 20.0f, sCaveHp = 20.0f;   // wet-path HP, Hz
    double sDlyTime = 1.0;
    float  sCaveLen[CaveReverb::kLines] {};
    float  sCavePre = 1.0f;
};

} // namespace blockwave
