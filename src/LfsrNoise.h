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
// Output is a 1-bit pulse train (+1 / -1), so the "only squares" rule holds.
//
// The LFSR clocks at a FIXED rate independent of note pitch (per spec):
// kClockHz = 1789773 / 54 Hz ≈ 33144 Hz — an NES-like timer division that
// sounds bright/white in long mode at all supported sample rates. The clock
// is run through a phase accumulator so any host sample rate works; steps are
// applied one at a time so golden sequences are deterministic.
class LfsrNoise
{
public:
    static constexpr double kClockHz = 1789773.0 / 54.0;

    void prepare (double sampleRate) noexcept
    {
        clockInc = kClockHz / sampleRate;
        reset();
    }

    void reset() noexcept
    {
        reg    = 1;
        phase  = 0.0;
        curOut = outputBit();
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
        return curOut;
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
};

} // namespace blockwave
