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
#include "BlockwaveParams.h"

namespace blockwave
{

// TPT / cytomic zero-delay-feedback state variable filter.
// LP24 cascades two LP12 stages (resonance on the first stage only).
// Coefficients are recomputed at control rate (Voice does this); the ZDF
// topology stays clean under fast cutoff modulation.
class SvfFilter
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        ic1a = ic2a = ic1b = ic2b = 0.0f;
    }

    void setParams (FilterType typeIn, float cutoffHz, float res) noexcept
    {
        type = typeIn;
        // Clamp cutoff to a stable range at every sample rate (44.1k–192k).
        const float maxHz = static_cast<float> (sr) * 0.49f;
        const float fc    = cutoffHz < 20.0f ? 20.0f : (cutoffHz > maxHz ? maxHz : cutoffHz);
        // Clamp resonance: k below ~0.12 self-oscillates violently near
        // Nyquist at high sample rates; 0..1 maps to k = 2..0.12.
        const float r = res < 0.0f ? 0.0f : (res > 1.0f ? 1.0f : res);
        k = 2.0f - 1.88f * r;

        g  = std::tan (3.14159265358979323846f * fc / static_cast<float> (sr));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        // Second (LP24) stage: same g, no resonance (k = 2).
        b1 = 1.0f / (1.0f + g * (g + 2.0f));
        b2 = g * b1;
        b3 = g * b2;
    }

    float process (float in) noexcept
    {
        // Stage 1
        const float v3 = in - ic2a;
        const float v1 = a1 * ic1a + a2 * v3;
        const float v2 = ic2a + a2 * ic1a + a3 * v3;
        ic1a = 2.0f * v1 - ic1a;
        ic2a = 2.0f * v2 - ic2a;

        switch (type)
        {
            case FilterType::lp12: return v2;
            case FilterType::bp:   return v1;
            case FilterType::hp:   return in - k * v1 - v2;
            case FilterType::lp24:
            default:
            {
                const float w3 = v2 - ic2b;
                const float w1 = b1 * ic1b + b2 * w3;
                const float w2 = ic2b + b2 * ic1b + b3 * w3;
                ic1b = 2.0f * w1 - ic1b;
                ic2b = 2.0f * w2 - ic2b;
                return w2;
            }
        }
    }

private:
    double sr = 44100.0;
    FilterType type = FilterType::lp24;
    float g = 0.0f, k = 2.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
    float ic1a = 0.0f, ic2a = 0.0f, ic1b = 0.0f, ic2b = 0.0f;
};

} // namespace blockwave
