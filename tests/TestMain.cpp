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

// BLOCKWAVE Phase-1 offline render tests. Pure C++ (no JUCE) so the suite
// builds fast and runs identically in Debug and Release.
//
// Golden files live in tests/golden/ (float32 raw + WAV artifacts for ears).
// The aliasing comparison numbers land in tests/reports/aliasing_report.txt.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <new>

#include "TestUtil.h"

// ---------------------------------------------------------------------------
// Global allocation guard. Counts every operator new/delete in the process;
// the audio-thread test asserts the count does not move across process()
// calls. Active in EVERY build config of this binary (Debug DoD requirement).
// ---------------------------------------------------------------------------
static std::atomic<long> gAllocCount { 0 };
static std::atomic<bool> gAllocGuardArmed { false };
static std::atomic<long> gGuardedAllocs { 0 };

static void* countedAlloc (std::size_t size)
{
    gAllocCount.fetch_add (1, std::memory_order_relaxed);
    if (gAllocGuardArmed.load (std::memory_order_relaxed))
        gGuardedAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    std::abort();
}

void* operator new (std::size_t size) { return countedAlloc (size); }
void* operator new[] (std::size_t size) { return countedAlloc (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

struct AllocGuard
{
    AllocGuard()  { gGuardedAllocs.store (0); gAllocGuardArmed.store (true); }
    ~AllocGuard() { gAllocGuardArmed.store (false); }
    long count() const { return gGuardedAllocs.load(); }
};

using namespace blockwave;
using namespace testutil;

static const std::string kTestDir = BLOCKWAVE_TEST_DIR;

// ---------------------------------------------------------------------------
static void test_pitch_accuracy()
{
    std::printf ("[pitch_accuracy]\n");
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int notes[]    = { 45, 57, 69, 81 };          // A2..A5

    for (const double sr : rates)
    {
        for (const int note : notes)
        {
            ParamSnapshot p;                            // defaults: OSC A square
            auto r = renderNote (p, note, sr, 1.4, 1.5);
            const double f0 = measureF0 (r.l, static_cast<int> (0.4 * sr),
                                         static_cast<int> (1.35 * sr), sr);
            const double ref = 440.0 * std::exp2 ((note - 69) / 12.0);
            const double cents = centsDiff (f0, ref);
            std::printf ("  sr=%6.0f note=%3d f0=%9.4f Hz ref=%9.4f Hz err=%+.3f cents\n",
                         sr, note, f0, ref, cents);
            CHECK_MSG (std::abs (cents) <= 1.0,
                       "pitch error %.3f cents exceeds 1 cent", cents);
        }
    }
}

// ---------------------------------------------------------------------------
static void test_aliasing_report()
{
    std::printf ("[aliasing_report]\n");
    const double sr = 44100.0;
    const int note = 96;                                // C7 ~ 2093 Hz
    const size_t fftN = 32768;

    ParamSnapshot base;
    base.filt_cutoff = 20000.0f;                        // effectively open
    base.env1_s = 1.0f;

    double aliasDb[2] = {}, fundDb[2] = {};
    for (int mode = 0; mode < 2; ++mode)                // 0 = polyBLEP, 1 = RAW
    {
        ParamSnapshot p = base;
        p.raw = mode == 1;
        auto r = renderNote (p, note, sr, 1.2, 1.2);

        const int skip = static_cast<int> (0.2 * sr);
        auto db = spectrumDb (r.l.data() + skip, fftN);
        const double binHz = sr / static_cast<double> (fftN);

        // Measured fundamental for the harmonic mask.
        const double f0 = measureF0 (r.l, skip, skip + static_cast<int> (fftN), sr);
        const int f0bin = static_cast<int> (f0 / binHz + 0.5);
        fundDb[mode] = db[static_cast<size_t> (f0bin)];

        double worst = -300.0;
        const int guard = 12;                           // Hann leakage guard, bins
        for (size_t i = static_cast<size_t> (200.0 / binHz); i < db.size(); ++i)
        {
            const double hz = static_cast<double> (i) * binHz;
            const double nearest = std::round (hz / f0) * f0;
            if (std::abs (hz - nearest) / binHz < guard)
                continue;                               // harmonic (or its skirt)
            worst = std::max (worst, db[i]);
        }
        aliasDb[mode] = worst - fundDb[mode];           // dB rel. fundamental
    }

    const double improvement = aliasDb[1] - aliasDb[0];
    std::filesystem::create_directories (kTestDir + "/reports");
    if (FILE* f = std::fopen ((kTestDir + "/reports/aliasing_report.txt").c_str(), "w"))
    {
        std::fprintf (f, "BLOCKWAVE aliasing report — C7 (MIDI 96, ~2093 Hz) @ 44100 Hz, FFT 32768 Hann\n");
        std::fprintf (f, "Worst non-harmonic partial relative to fundamental:\n");
        std::fprintf (f, "  RAW (naive)  : %+7.2f dBc\n", aliasDb[1]);
        std::fprintf (f, "  polyBLEP     : %+7.2f dBc\n", aliasDb[0]);
        std::fprintf (f, "  improvement  : %7.2f dB\n", improvement);
        std::fclose (f);
    }
    std::printf ("  RAW alias %+.2f dBc, polyBLEP alias %+.2f dBc, improvement %.2f dB\n",
                 aliasDb[1], aliasDb[0], improvement);

    CHECK_MSG (improvement >= 15.0,
               "polyBLEP only %.2f dB better than RAW (need >= 15)", improvement);
    CHECK_MSG (aliasDb[0] <= -45.0,
               "polyBLEP worst alias %.2f dBc (need <= -45)", aliasDb[0]);
}

// ---------------------------------------------------------------------------
static void goldenCompare (const std::string& name, const std::vector<float>& l,
                           const std::vector<float>& r, int sr)
{
    std::filesystem::create_directories (kTestDir + "/golden");
    const std::string f32Path = kTestDir + "/golden/" + name + ".f32";
    const std::string wavPath = kTestDir + "/golden/" + name + ".wav";

    std::vector<float> golden;
    if (! readF32 (f32Path, golden))
    {
        writeF32 (f32Path, l);
        writeWav16 (wavPath, l, r, sr);
        std::printf ("  created golden %s (%zu samples) — re-run to verify\n",
                     f32Path.c_str(), l.size());
        return;
    }
    CHECK_MSG (golden.size() == l.size(), "%s: golden length %zu != render %zu",
               name.c_str(), golden.size(), l.size());
    float maxDiff = 0.0f;
    const size_t n = std::min (golden.size(), l.size());
    for (size_t i = 0; i < n; ++i)
        maxDiff = std::max (maxDiff, std::abs (golden[i] - l[i]));
    std::printf ("  %s: max |diff| vs golden = %.3g\n", name.c_str(), static_cast<double> (maxDiff));
    CHECK_MSG (maxDiff <= 1.0e-6f, "%s: render deviates from golden by %.3g",
               name.c_str(), static_cast<double> (maxDiff));
}

static void test_hard_sync_golden()
{
    std::printf ("[hard_sync_golden]\n");
    ParamSnapshot p;
    p.oscA_on = true;  p.oscA_level = 0.5f;
    p.oscB_on = true;  p.oscB_level = 0.8f;
    p.oscB_sync = true;
    p.oscB_oct = 1;    p.oscB_semi = 5;                 // ~2.52x master, classic rip
    p.env1_s = 1.0f;
    auto r = renderNote (p, 45, 48000.0, 0.5, 0.6);
    goldenCompare ("hard_sync_A2_48k", r.l, r.r, 48000);
}

// ---------------------------------------------------------------------------
static void test_lfsr()
{
    std::printf ("[lfsr]\n");

    // Long mode: maximal 15-bit sequence, period 32767.
    {
        LfsrNoise n;
        n.prepare (48000.0);
        const auto initial = n.state();
        long period = 0;
        do { n.step (false); ++period; } while (n.state() != initial && period <= 40000);
        std::printf ("  long-mode period = %ld\n", period);
        CHECK_MSG (period == 32767, "long-mode period %ld != 32767", period);
    }

    // Short mode: NES metallic loop (93 steps from seed 1).
    {
        LfsrNoise n;
        n.prepare (48000.0);
        const auto initial = n.state();
        long period = 0;
        do { n.step (true); ++period; } while (n.state() != initial && period <= 40000);
        std::printf ("  short-mode period = %ld\n", period);
        CHECK_MSG (period == 93, "short-mode period %ld != 93", period);
    }

    // Golden renders, both modes.
    for (int mode = 0; mode < 2; ++mode)
    {
        ParamSnapshot p;
        p.oscA_on = false;
        p.noise_on = true;
        p.noise_level = 0.8f;
        p.noise_mode = mode == 0 ? NoiseMode::longMode : NoiseMode::shortMode;
        p.env1_s = 1.0f;
        auto r = renderNote (p, 60, 48000.0, 0.25, 0.3);
        goldenCompare (mode == 0 ? "lfsr_long_48k" : "lfsr_short_48k", r.l, r.r, 48000);
    }
}

// ---------------------------------------------------------------------------
static void test_adsr_shape()
{
    std::printf ("[adsr_shape]\n");
    const double sr = 48000.0;
    AdsrEnv env;
    env.prepare (sr);
    env.setTimes (0.1f, 0.2f, 0.5f, 0.15f);

    env.noteOn();
    std::vector<float> levels;
    for (int i = 0; i < static_cast<int> (sr); ++i)
        levels.push_back (env.tick());

    const auto at = [&] (double sec) { return levels[static_cast<size_t> (sec * sr)]; };

    // Attack peak (1.0) must land at ~the programmed attack time; decay
    // starts immediately after, so sample the peak itself.
    size_t peakIdx = 0;
    for (size_t i = 1; i < levels.size(); ++i)
        if (levels[i] > levels[peakIdx]) peakIdx = i;
    const double peakMs = static_cast<double> (peakIdx) / sr * 1000.0;
    std::printf ("  attack peak %.4f at %.1f ms\n",
                 static_cast<double> (levels[peakIdx]), peakMs);
    CHECK_MSG (at (0.010) < 0.35f, "attack too fast: %.3f at 10 ms", static_cast<double> (at (0.010)));
    CHECK_MSG (levels[peakIdx] >= 0.999f, "attack never reached 1.0 (peak %.3f)",
               static_cast<double> (levels[peakIdx]));
    CHECK_MSG (peakMs > 80.0 && peakMs < 120.0, "attack time off: peak at %.1f ms (want ~100)", peakMs);
    // Exponential decay: by attack+decay time it should be within 10% of sustain.
    const float dec = at (0.32);
    CHECK_MSG (std::abs (dec - 0.5f) < 0.05f, "decay landed at %.3f, expected ~0.5", static_cast<double> (dec));
    CHECK_MSG (std::abs (at (0.9) - 0.5f) < 0.01f, "sustain drifted: %.3f", static_cast<double> (at (0.9)));

    // Release: below -60 dB within ~1.6x release time, then hard 0 (denormal-safe).
    env.noteOff();
    int toSilent = -1, toZero = -1;
    for (int i = 0; i < static_cast<int> (sr); ++i)
    {
        const float v = env.tick();
        if (toSilent < 0 && v < 0.001f) toSilent = i;
        if (toZero < 0 && v == 0.0f)    { toZero = i; break; }
    }
    std::printf ("  release: -60 dB at %.1f ms, hard zero at %.1f ms\n",
                 toSilent / sr * 1000.0, toZero / sr * 1000.0);
    CHECK_MSG (toSilent > 0 && toSilent / sr < 0.4, "release too slow (-60 dB at %.3f s)", toSilent / sr);
    CHECK_MSG (toZero > 0, "release never reached hard zero (denormal risk)");
    CHECK_MSG (! env.isActive(), "env still active after full release");

    // Click-free retrigger: attack must depart from the current level.
    env.noteOn();
    float lvl = 0.0f;
    for (int i = 0; i < 2000; ++i) lvl = env.tick();       // part-way up the attack
    env.noteOff();
    for (int i = 0; i < 200; ++i) lvl = env.tick();        // partial release
    const float before = lvl;
    env.noteOn();
    const float after = env.tick();
    CHECK_MSG (std::abs (after - before) < 0.01f,
               "retrigger jumped %.3f -> %.3f", static_cast<double> (before), static_cast<double> (after));
}

// ---------------------------------------------------------------------------
static void test_block_size_invariance()
{
    std::printf ("[block_size_invariance]\n");
    ParamSnapshot p;
    p.oscB_on = true; p.oscB_sync = true; p.oscB_semi = 7;
    p.sub_on = true; p.noise_on = true;
    p.uni_count = 4; p.uni_detune = 20.0f;
    p.filt_cutoff = 2500.0f; p.filt_res = 0.4f; p.filt_env = 0.5f;

    auto ref = renderNote (p, 48, 48000.0, 0.3, 0.4, 512);
    const int sizes[] = { 16, 61, 128, 1024, 4096 };
    for (const int bs : sizes)
    {
        auto r = renderNote (p, 48, 48000.0, 0.3, 0.4, bs);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < ref.l.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (ref.l[i] - r.l[i]));
        std::printf ("  block %4d vs 512: max |diff| = %.3g\n", bs, static_cast<double> (maxDiff));
        CHECK_MSG (maxDiff == 0.0f, "block size %d changes output by %.3g", bs,
                   static_cast<double> (maxDiff));
    }
}

// ---------------------------------------------------------------------------
static void test_no_allocation_on_audio_thread()
{
    std::printf ("[no_allocation]\n");
    BlockwaveEngine engine;
    ParamSnapshot p;
    p.oscB_on = true; p.oscB_sync = true; p.sub_on = true; p.noise_on = true;
    p.uni_count = 8; p.poly_count = 16; p.lfo1_pwm = 0.5f; p.lfo2_amt = 0.5f;
    engine.setParams (p);
    engine.prepare (48000.0, 512);

    float l[512], r[512];
    {
        AllocGuard guard;
        for (int block = 0; block < 200; ++block)
        {
            if (block % 10 == 0) engine.noteOn (36 + block % 48, 0.8f);
            if (block % 14 == 0) engine.noteOff (36 + (block - 70) % 48);
            engine.process (l, r, 512);
        }
        engine.allNotesOff();
        engine.process (l, r, 512);
        CHECK_MSG (guard.count() == 0,
                   "audio path allocated %ld times", guard.count());
    }
    std::printf ("  0 allocations across 201 blocks with notes + stealing\n");
}

// ---------------------------------------------------------------------------
static void test_cpu_worst_case()
{
    std::printf ("[cpu_worst_case]\n");
    // Worst realtime patch: everything on, 16 held notes, unison 8 requested
    // (engine caps to 4 with poly 16 -> 64 stacks; see BlockwaveEngine.h).
    ParamSnapshot p;
    p.oscB_on = true; p.oscB_sync = true; p.sub_on = true; p.noise_on = true;
    p.uni_count = 8; p.poly_count = 16;
    p.lfo1_pwm = 0.4f; p.lfo2_amt = 0.5f;
    p.env1_s = 1.0f;

    BlockwaveEngine engine;
    engine.setParams (p);
    engine.prepare (44100.0, 128);
    for (int i = 0; i < 16; ++i)
        engine.noteOn (36 + i * 3, 0.9f);

    float l[128], r[128];
    const int blocks = static_cast<int> (5.0 * 44100.0 / 128.0);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
        engine.process (l, r, 128);
    const auto t1 = std::chrono::steady_clock::now();

    const double renderSec = std::chrono::duration<double> (t1 - t0).count();
    const double audioSec  = blocks * 128.0 / 44100.0;
    const double ratio = renderSec / audioSec;
    std::printf ("  16 voices, capped 4-way unison (64 stacks), all sources on:\n");
    std::printf ("  %.2f s audio in %.3f s CPU -> %.1f%% of one core @ 44.1k/128\n",
                 audioSec, renderSec, ratio * 100.0);
    CHECK_MSG (ratio < 0.9, "worst case not realtime: %.1f%%", ratio * 100.0);
}

// ---------------------------------------------------------------------------
int main()
{
    test_pitch_accuracy();
    test_aliasing_report();
    test_hard_sync_golden();
    test_lfsr();
    test_adsr_shape();
    test_block_size_invariance();
    test_no_allocation_on_audio_thread();
    test_cpu_worst_case();

    std::printf ("\n%d checks, %d failures\n", state().checks, state().failures);
    return state().failures == 0 ? 0 : 1;
}
