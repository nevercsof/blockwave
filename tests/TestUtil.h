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

// Test utilities: minimal WAV writer (artifact listening), raw float32 golden
// files, radix-2 FFT, zero-crossing pitch measurement, and a render helper.

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <complex>

#include "BlockwaveEngine.h"

namespace testutil
{

// ---- 16-bit PCM WAV writer (artifacts for human listening) -----------------
inline bool writeWav16 (const std::string& path, const std::vector<float>& l,
                        const std::vector<float>& r, int sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;
    const bool stereo = ! r.empty();
    const std::uint16_t channels = stereo ? 2 : 1;
    const std::uint32_t frames = static_cast<std::uint32_t> (l.size());
    const std::uint32_t dataBytes = frames * channels * 2u;
    const std::uint32_t byteRate = static_cast<std::uint32_t> (sampleRate) * channels * 2u;

    const auto w16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const auto w32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };

    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (1); w16 (channels);
    w32 (static_cast<std::uint32_t> (sampleRate)); w32 (byteRate);
    w16 (static_cast<std::uint16_t> (channels * 2)); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);

    for (std::uint32_t i = 0; i < frames; ++i)
    {
        const auto conv = [] (float x)
        {
            const float c = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
            return static_cast<std::int16_t> (std::lround (c * 32767.0f));
        };
        const std::int16_t sl = conv (l[i]);
        std::fwrite (&sl, 2, 1, f);
        if (stereo)
        {
            const std::int16_t sr = conv (r[i]);
            std::fwrite (&sr, 2, 1, f);
        }
    }
    std::fclose (f);
    return true;
}

// ---- raw float32 golden files ----------------------------------------------
inline bool readF32 (const std::string& path, std::vector<float>& out)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;
    std::fseek (f, 0, SEEK_END);
    const long bytes = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);
    out.resize (static_cast<size_t> (bytes) / 4);
    const size_t got = std::fread (out.data(), 4, out.size(), f);
    std::fclose (f);
    return got == out.size();
}

inline bool writeF32 (const std::string& path, const std::vector<float>& in)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;
    std::fwrite (in.data(), 4, in.size(), f);
    std::fclose (f);
    return true;
}

// ---- radix-2 complex FFT (in place) ----------------------------------------
inline void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Hann-windowed magnitude spectrum in dB (relative, unnormalised).
inline std::vector<double> spectrumDb (const float* x, size_t n)
{
    std::vector<std::complex<double>> buf (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846
                                               * static_cast<double> (i) / static_cast<double> (n));
        buf[i] = std::complex<double> (static_cast<double> (x[i]) * w, 0.0);
    }
    fft (buf);
    std::vector<double> db (n / 2);
    for (size_t i = 0; i < n / 2; ++i)
        db[i] = 20.0 * std::log10 (std::abs (buf[i]) + 1e-12);
    return db;
}

// ---- pitch via interpolated rising zero crossings ---------------------------
// Returns f0 in Hz measured over [start, end) sample range, or 0 on failure.
inline double measureF0 (const std::vector<float>& x, int start, int end, double sr)
{
    double firstT = -1.0, lastT = -1.0;
    long crossings = -1;
    for (int i = start + 1; i < end; ++i)
    {
        if (x[static_cast<size_t> (i - 1)] < 0.0f && x[static_cast<size_t> (i)] >= 0.0f)
        {
            const double a = x[static_cast<size_t> (i - 1)];
            const double b = x[static_cast<size_t> (i)];
            const double t = (static_cast<double> (i) - 1.0) + (-a) / (b - a);
            if (firstT < 0.0) firstT = t;
            lastT = t;
            ++crossings;
        }
    }
    if (crossings < 2)
        return 0.0;
    return static_cast<double> (crossings) * sr / (lastT - firstT);
}

inline double centsDiff (double f, double ref)
{
    return 1200.0 * std::log2 (f / ref);
}

// ---- engine render helper ---------------------------------------------------
struct Rendered { std::vector<float> l, r; };

inline Rendered renderNote (const blockwave::ParamSnapshot& p, int note, double sr,
                            double noteSec, double totalSec,
                            int blockSize = 512, double bpm = 120.0, float vel = 1.0f)
{
    blockwave::BlockwaveEngine engine;
    engine.setParams (p);
    engine.setTempo (bpm);
    engine.prepare (sr, blockSize);

    const int total = static_cast<int> (totalSec * sr);
    const int offAt = static_cast<int> (noteSec * sr);
    Rendered out;
    out.l.assign (static_cast<size_t> (total), 0.0f);
    out.r.assign (static_cast<size_t> (total), 0.0f);

    engine.noteOn (note, vel);
    int pos = 0;
    bool offSent = false;
    while (pos < total)
    {
        if (! offSent && pos >= offAt) { engine.noteOff (note); offSent = true; }
        int n = std::min (blockSize, total - pos);
        if (! offSent && pos + n > offAt)
            n = offAt - pos;
        engine.process (out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

// ---- micro test framework ---------------------------------------------------
struct TestState
{
    int failures = 0;
    int checks = 0;
};

inline TestState& state() { static TestState s; return s; }

#define CHECK_MSG(cond, ...)                                                     \
    do {                                                                         \
        ++testutil::state().checks;                                              \
        if (! (cond)) {                                                          \
            ++testutil::state().failures;                                        \
            std::printf ("  FAIL %s:%d: ", __FILE__, __LINE__);                  \
            std::printf (__VA_ARGS__);                                           \
            std::printf ("\n");                                                  \
        }                                                                        \
    } while (false)

} // namespace testutil
