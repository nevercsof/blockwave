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

#include <cmath>
#include "PolyBlepOsc.h"

namespace blockwave
{

// The recipe-discovery confirmation sound (ROADMAP Phase 4 DoD: "discovery
// toast + jingle"). This is UI feedback, NOT a musical voice:
//
//  - it is synthesized from fixed constants, so it is identical whatever the
//    patch is doing (no APVTS read, no craft state, no velocity, no tempo);
//  - the processor mixes it in AFTER the engine's master gain and softclip, so
//    the FX chain, master gain and MIDI have no effect on it;
//  - it is a finite 3-step arpeggio (~270 ms) — it cannot stall an offline
//    render, and it is only ever triggered from the editor, so tools/render
//    never produces it;
//  - when idle, tick() returns exactly 0.0f and the processor does not touch
//    the buffer at all (bit-transparent null test);
//  - every step opens and closes on a 5 ms linear fade and the very first and
//    very last sample of each step have envelope 0, so there are no clicks;
//  - peak is kPeak (-18 dBFS), leaving headroom under the softclip ceiling.
//
// A polyBLEP square is used rather than a naive one for the same reason the
// oscillators use it: at 2 kHz on a 44.1 kHz session a naive square is a spray
// of aliases, and this is the sound that says "you found something".
//
// Audio thread only, except prepare().
class DiscoveryJingle
{
public:
    static constexpr int   kNumSteps    = 3;
    static constexpr float kStepSeconds = 0.09f;         // ~90 ms per step
    static constexpr float kFadeSeconds = 0.005f;        // ~5 ms in and out
    static constexpr float kPeak        = 0.12589254f;   // -18 dBFS

    // C6 - G6 - C7: a rising "found it" arpeggio, safely below Nyquist at the
    // lowest supported sample rate (2093 Hz vs 22050 Hz at 44.1k).
    static constexpr float kStepHz[kNumSteps] = { 1046.502f, 1567.982f, 2093.005f };

    // Non-realtime (prepareToPlay). Sizes everything; there is nothing to
    // allocate.
    void prepare (double sampleRate) noexcept
    {
        stepSamples = static_cast<int> (std::lround (kStepSeconds * sampleRate));
        if (stepSamples < 4)
            stepSamples = 4;
        fadeSamples = static_cast<int> (std::lround (kFadeSeconds * sampleRate));
        if (fadeSamples < 1)
            fadeSamples = 1;
        if (fadeSamples > stepSamples / 2)
            fadeSamples = stepSamples / 2;

        for (int i = 0; i < kNumSteps; ++i)
        {
            double inc = static_cast<double> (kStepHz[i]) / sampleRate;
            if (inc > 0.45)                              // paranoia; never hit
                inc = 0.45;
            stepInc[i] = inc;
        }
        stop();
    }

    // Audio thread. Restarts from the beginning; a jingle already playing is
    // cut and re-attacked from silence (the envelope starts at 0, so this is
    // click-free too).
    void start() noexcept
    {
        osc.reset (0.0);
        step = 0;
        sampleInStep = 0;
        active = true;
    }

    void stop() noexcept
    {
        active = false;
        step = 0;
        sampleInStep = 0;
    }

    bool isActive() const noexcept { return active; }

    // Audio thread. Returns exactly 0.0f while idle.
    float tick() noexcept
    {
        if (! active)
            return 0.0f;

        const float env = envelopeAt (sampleInStep);
        const float s = osc.tick (stepInc[step], 0.5f, false).sample;

        if (++sampleInStep >= stepSamples)
        {
            sampleInStep = 0;
            if (++step >= kNumSteps)
                active = false;          // the sample just emitted had env 0
        }
        return s * kPeak * env;
    }

private:
    // Triangular fade window: 0 at the first and last sample of every step,
    // 1 in the middle. Linear, so no denormals and no recursive state.
    float envelopeAt (int s) const noexcept
    {
        const int fromEnd = stepSamples - 1 - s;
        const int d = s < fromEnd ? s : fromEnd;
        if (d >= fadeSamples)
            return 1.0f;
        return static_cast<float> (d) / static_cast<float> (fadeSamples);
    }

    PolyBlepOsc osc;
    double stepInc[kNumSteps] { 0.0, 0.0, 0.0 };
    int stepSamples = 4410;
    int fadeSamples = 220;
    int step = 0;
    int sampleInStep = 0;
    bool active = false;
};

} // namespace blockwave
