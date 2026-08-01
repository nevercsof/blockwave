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

// Exponential-segment ADSR.
// - Attack: one-pole toward an overshoot target (1.3) so it reaches 1.0 in
//   the programmed time with a musical convex curve.
// - Decay/Release: one-pole toward sustain/zero; time = ~3 time constants
//   (95% travelled).
// - Retrigger is click-free: attack always departs from the CURRENT level.
// - Release tail is denormal-safe: snaps to hard 0 below -100 dB and idles.
class AdsrEnv
{
public:
    enum class State { idle, attack, decay, sustain, release };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        level = 0.0f;
        state = State::idle;
    }

    void setTimes (float aSec, float dSec, float sLevel, float rSec) noexcept
    {
        sustain = sLevel < 0.0f ? 0.0f : (sLevel > 1.0f ? 1.0f : sLevel);
        // ln(1.3 / 0.3): attack tau such that level hits 1.0 at t = aSec.
        constexpr float kAttackStretch = 1.46633707f;
        aCoef = coefFor (aSec, kAttackStretch);
        dCoef = coefFor (dSec, 3.0f);
        rCoef = coefFor (rSec, 3.0f);
    }

    void noteOn() noexcept  { state = State::attack; }           // from current level
    void noteOff() noexcept { if (state != State::idle) state = State::release; }
    void kill() noexcept    { reset(); }

    bool  isActive() const noexcept { return state != State::idle; }
    bool  isReleasing() const noexcept { return state == State::release; }
    float current() const noexcept { return level; }
    State currentState() const noexcept { return state; }

    float tick() noexcept
    {
        switch (state)
        {
            case State::attack:
                level += aCoef * (1.3f - level);
                if (level >= 1.0f) { level = 1.0f; state = State::decay; }
                break;
            case State::decay:
                level += dCoef * (sustain - level);
                if (level - sustain < 1e-4f) { level = sustain; state = State::sustain; }
                break;
            case State::sustain:
                level = sustain;
                if (sustain <= 0.0f) state = State::idle;
                break;
            case State::release:
                level += rCoef * (0.0f - level);
                if (level < 1e-5f) { level = 0.0f; state = State::idle; }
                break;
            case State::idle:
            default:
                level = 0.0f;
                break;
        }
        return level;
    }

private:
    float coefFor (float seconds, float stretch) const noexcept
    {
        const float t = seconds < 0.0005f ? 0.0005f : seconds;   // >= 0.5 ms, click guard
        const float tau = t / stretch;
        return 1.0f - std::exp (-1.0f / (tau * static_cast<float> (sr)));
    }

    double sr = 44100.0;
    float level = 0.0f, sustain = 0.8f;
    float aCoef = 0.01f, dCoef = 0.001f, rCoef = 0.001f;
    State state = State::idle;
};

} // namespace blockwave
