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

#include <cstdint>

namespace blockwave
{

// NES-APU-style 15-bit LFSR noise.
//
// Long mode: feedback = bit0 XOR bit1 (32767-step maximal sequence, white-ish).
// Short mode: feedback = bit0 XOR bit6 (93-step metallic loop).
// Output is a 1-bit pulse train (+1 / -1) — the "only squares" rule holds —
// followed by the DC blocker described below (a filter, not a waveshape).
//
// The LFSR clocks at a FIXED rate independent of note pitch (per spec):
// kClockHz = 1789773 / 54 Hz ≈ 33144 Hz — an NES-like timer division that
// sounds bright/white in long mode at all supported sample rates. The clock
// is run through a phase accumulator so any host sample rate works; steps are
// applied one at a time so golden sequences are deterministic.
//
// DC blocker: the short-mode 93-step loop has grossly unequal +1/-1 counts
// (mean ≈ +0.66 — a huge DC offset straight into the filter and FX chain),
// and even the long sequence carries a 1/32767 bias. tick() therefore runs
// the raw bit output through a one-pole high-pass at ~8 Hz. Applied to BOTH
// modes (documented choice): one code path, bias-free by construction, and
// at 8 Hz it is transparent for everything audible. The LFSR sequence itself
// (step()/state(), the golden-sequence tests) is untouched.
class LfsrNoise
{
public:
    static constexpr double kClockHz = 1789773.0 / 54.0;
    static constexpr double kDcBlockHz = 8.0;

    void prepare (double sampleRate) noexcept
    {
        clockInc = kClockHz / sampleRate;
        // One-pole DC blocker coefficient: y[n] = x[n] - x[n-1] + R*y[n-1].
        dcR = static_cast<float> (1.0 - 2.0 * 3.14159265358979323846
                                        * kDcBlockHz / sampleRate);
        reset();
    }

    void reset() noexcept
    {
        reg    = 1;
        phase  = 0.0;
        curOut = outputBit();
        dcX1   = 0.0f;
        dcY1   = 0.0f;
    }

    // ---- per-voice decorrelation (noise-polyphony fix, Phase-6 feedback) ----
    //
    // Every voice used to reset to seed 1, so a quantized chord played N
    // perfectly IDENTICAL noise streams that summed coherently (+6 dB per
    // doubling) — the engine's 1/sqrt(N) polyphony compensation assumes
    // UNCORRELATED streams. Fix: voice k starts k * 7919 raw steps into the
    // sequence of its current mode.
    //   long  mode: an m-sequence's periodic autocorrelation at any nonzero
    //               lag is -1/32767, so any distinct offsets decorrelate;
    //               7919 steps ≈ 239 ms keeps comb spacing ~4 Hz (inaudible).
    //               gcd(7919, 32767) = 1 -> all 16 offsets are distinct.
    //   short mode: advancing from seed 1 stays inside the same 93-step
    //               metallic loop (same timbre), phase-shifted by
    //               (k * 7919) mod 93 = 14k mod 93 — distinct for k = 0..15.
    // The offsets are PRECOMPUTED register states (table below) so a noteOn
    // costs O(1) on the audio thread; the table is verified against live
    // step() advancement by tests/TestMain.cpp (test_lfsr_voice_seeds).
    // Voice 0 keeps seed 1 in both modes — single-note renders (all goldens)
    // are bit-identical to the pre-fix engine.
    static constexpr int kMaxSeedVoices = 16;
    static constexpr std::uint16_t kVoiceSeedLong[kMaxSeedVoices] =
        { 1, 12738, 29188, 24803, 10768, 9117, 22023, 25241,
          2312, 29028, 27123, 89, 8759, 23524, 18921, 25922 };
    static constexpr std::uint16_t kVoiceSeedShort[kMaxSeedVoices] =
        { 1, 1026, 16420, 4681, 9216, 2336, 520, 1024,
          18464, 4609, 146, 16672, 4169, 16, 16416, 577 };

    // reset() with the voice's decorrelation seed for the given mode.
    // voiceIndex 0 (and any out-of-range index) is plain reset().
    void resetForVoice (int voiceIndex, bool shortMode) noexcept
    {
        reset();
        if (voiceIndex > 0 && voiceIndex < kMaxSeedVoices)
        {
            reg    = shortMode ? kVoiceSeedShort[voiceIndex]
                               : kVoiceSeedLong[voiceIndex];
            curOut = outputBit();
        }
    }

    // shortMode selects the alternate tap (bit 6).
    float tick (bool shortMode) noexcept
    {
        phase += clockInc;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            step (shortMode);
        }
        const float y = curOut - dcX1 + dcR * dcY1;   // DC blocker (see header)
        dcX1 = curOut;
        dcY1 = y;
        return y;
    }

    // One raw shift-register step (public for the golden-sequence test).
    void step (bool shortMode) noexcept
    {
        const std::uint16_t bit0 = reg & 1u;
        const std::uint16_t tap  = shortMode ? ((reg >> 6) & 1u) : ((reg >> 1) & 1u);
        const std::uint16_t fb   = bit0 ^ tap;
        reg = static_cast<std::uint16_t> ((reg >> 1) | (fb << 14));
        curOut = outputBit();
    }

    std::uint16_t state() const noexcept { return reg; }

private:
    float outputBit() const noexcept { return (reg & 1u) ? -1.0f : 1.0f; }

    std::uint16_t reg = 1;
    double clockInc   = 0.0;
    double phase      = 0.0;
    float  curOut     = 1.0f;
    float  dcR        = 0.999f;   // DC blocker state (see prepare/tick)
    float  dcX1       = 0.0f;
    float  dcY1       = 0.0f;
};

} // namespace blockwave
