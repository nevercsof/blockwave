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

namespace blockwave
{

// PolyBLEP pulse oscillator, event-based with a 4-point cubic-B-spline BLEP
// kernel (support [-2,+2] samples around each discontinuity).
//
// The oscillator runs with TWO SAMPLES of latency: each tick pushes the new
// naive sample into a short pipeline and pops the oldest, so every
// discontinuity — rising edge at phase wrap, falling edge at the pulse-width
// crossing, and the hard-sync reset — can be corrected on the two samples
// before it, the sample after it, and one future sample.
//
// For a step of size d at time te, with t = (sample time - te) in samples,
// the residual added to each nearby sample is d * r(t), where r is the cubic
// B-spline step residual:
//     F(t) = (t+2)^4 / 24                                   for t in [-2,-1]
//     F(t) = 1/24 + (-3t^4/4 - 2t^3 + 4t + 11/4) / 6        for t in [-1, 0]
//     r(t) = F(t)  for t <  0        (F is the B-spline step response)
//     r(t) = -F(-t) for t >= 0       (odd symmetry about the step)
// An event detected s samples before the newest sample (s in [0,1)) touches
// samples at t = s-2, s-1, s, s+1.
//
// All oscillators share the same 2-sample latency, so the mix stays coherent.
// RAW mode skips corrections (intentional aliasing) but keeps the pipeline so
// toggling RAW never shifts phase alignment.
//
// Known simplification (documented): if the slave's own pw/wrap edge falls in
// the same sample as a hard-sync reset, only the reset is BLEPed. The
// residual aliasing is inaudible next to the sync spectrum itself.
class PolyBlepOsc
{
public:
    void reset (double startPhase = 0.0) noexcept
    {
        phase   = startPhase;
        buf0 = buf1 = 0.0f;
        pending = 0.0f;
        primed  = 0;
    }

    struct Tick
    {
        float sample;       // delayed-by-two, BLEP-corrected output
        bool  wrapped;      // wrapped during this tick (sync master signal)
        float wrapFrac;     // samples between the wrap and the newest sample [0,1)
    };

    // inc = frequency / sampleRate (caller clamps to < 0.5).
    // pw in (0,1). syncFrac >= 0 requests a hard-sync reset syncFrac samples
    // before the newest sample (taken from the master's wrapFrac).
    Tick tick (double inc, float pw, bool raw, float syncFrac = -1.0f) noexcept
    {
        const double oldPhase = phase;
        phase += inc;

        bool  wrapped  = false;
        float wrapFrac = 0.0f;

        float cur;              // newest naive sample
        float curCorr  = 0.0f;  // correction for newest sample      (t = s)
        float b1Corr   = 0.0f;  // correction for buf1 (1 sample old, t = s-1)
        float b0Corr   = 0.0f;  // correction for buf0 (2 samples old, t = s-2)
        float nextCorr = 0.0f;  // correction for the NEXT sample     (t = s+1)

        const auto naive = [pw] (double p) noexcept
        {
            return p < static_cast<double> (pw) ? 1.0f : -1.0f;
        };

        const auto addEvent = [&] (float s, float d) noexcept
        {
            // s in [0,1): samples between the event and the newest sample.
            b0Corr   += d * residual (s - 2.0f);
            b1Corr   += d * residual (s - 1.0f);
            curCorr  += d * residual (s);
            nextCorr += d * residual (s + 1.0f);
        };

        if (syncFrac >= 0.0f)
        {
            // Value the free-running slave would have had at the reset instant.
            double atReset = oldPhase + (1.0 - static_cast<double> (syncFrac)) * inc;
            if (atReset >= 1.0) atReset -= 1.0;
            const float before = naive (atReset);
            const float after  = 1.0f;               // reset lands at phase 0 (< pw)
            const float d      = after - before;

            phase = static_cast<double> (syncFrac) * inc;
            cur   = naive (phase);

            if (d != 0.0f && ! raw)
                addEvent (syncFrac, d);
        }
        else
        {
            if (phase >= 1.0)
            {
                phase  -= 1.0;
                wrapped = true;
                wrapFrac = static_cast<float> (phase / inc);   // in [0,1)
                if (! raw)
                    addEvent (wrapFrac, 2.0f);                 // rising edge
            }

            cur = naive (phase);

            // Falling edge at the pw crossing, in this sample's unwrapped span.
            const double pwd = static_cast<double> (pw);
            double crossDist = -1.0;                           // phase past pw
            if (! wrapped)
            {
                if (oldPhase < pwd && phase >= pwd)
                    crossDist = phase - pwd;
            }
            else if (oldPhase < pwd)
            {
                crossDist = (1.0 - pwd) + phase;               // crossed pre-wrap
            }
            if (crossDist >= 0.0 && ! raw)
            {
                const float s = static_cast<float> (crossDist / inc);
                if (s < 1.0f)
                    addEvent (s, -2.0f);                       // falling edge
            }
        }

        // Remove the pulse-width DC offset so PWM does not thump.
        const float dc = 2.0f * pw - 1.0f;

        const float out = primed >= 2 ? (buf0 + b0Corr) : 0.0f;
        buf0 = buf1 + b1Corr;
        buf1 = cur - dc + curCorr + pending;
        pending = nextCorr;
        if (primed < 2) ++primed;
        return { out, wrapped, wrapFrac };
    }

private:
    // Cubic B-spline step residual r(t), t in [-2, 2].
    static float residual (float t) noexcept
    {
        const float a = t < 0.0f ? t : -t;      // fold to negative side
        float f;
        if (a <= -2.0f)
        {
            f = 0.0f;
        }
        else if (a <= -1.0f)
        {
            const float x = a + 2.0f;
            f = x * x * x * x * (1.0f / 24.0f);
        }
        else
        {
            const float a2 = a * a;
            f = 1.0f / 24.0f
              + (-0.75f * a2 * a2 - 2.0f * a2 * a + 4.0f * a + 2.75f) * (1.0f / 6.0f);
        }
        return t < 0.0f ? f : -f;
    }

    double phase = 0.0;
    float  buf0 = 0.0f, buf1 = 0.0f, pending = 0.0f;
    int    primed = 0;
};

} // namespace blockwave
