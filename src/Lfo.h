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

#include "BlockwaveParams.h"

namespace blockwave
{

// Global (per-engine) LFO. Tempo sync: rate expressed in whole notes, so
// freqHz = bpm / (240 * wholeNotes). Free: rate is Hz directly.
// S&H uses a private xorshift32 RNG with a fixed seed for deterministic
// offline renders; each engine instance is independent (no static state).
class Lfo
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0;
        rng   = 0x2A6F1D4Bu;
        shVal = nextRand();
    }

    void setRate (float rate, bool sync, double bpm) noexcept
    {
        double hz;
        if (sync)
        {
            const double whole = rate < 1.0e-3f ? 1.0e-3 : static_cast<double> (rate);
            hz = bpm / (240.0 * whole);
        }
        else
        {
            hz = static_cast<double> (rate);
        }
        if (hz < 0.0)  hz = 0.0;
        if (hz > 40.0) hz = 40.0;
        inc = hz / sr;
    }

    float tick (LfoShape shape) noexcept
    {
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            shVal = nextRand();
        }
        switch (shape)
        {
            case LfoShape::square:     return phase < 0.5 ? 1.0f : -1.0f;
            case LfoShape::sampleHold: return shVal;
            case LfoShape::tri:
            default:
            {
                const float p = static_cast<float> (phase);
                return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
            }
        }
    }

private:
    float nextRand() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<float> (rng & 0xFFFFFFu) * (2.0f / 16777215.0f) - 1.0f;
    }

    double sr = 44100.0, phase = 0.0, inc = 0.0;
    std::uint32_t rng = 0x2A6F1D4Bu;
    float shVal = 0.0f;
};

} // namespace blockwave
