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

#include "FxTests.h"    // Phase-5 FX suite (uses kTestDir; same strict TU)
#include "CraftTests.h" // Phase-4 CRAFT suite (pure CraftEngine)
#include "UiAudioTests.h" // Phase-4 UI MIDI inbox + discovery jingle

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

    // DC blocker (~8 Hz one-pole HP in LfsrNoise::tick): the raw short-mode
    // 93-step loop has a mean of ~ +0.66 — a massive DC offset — while the
    // blocked tick() output must average to ~0 in both modes. The raw
    // sequence itself (step()/state()) is deliberately untouched.
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool shortMode = mode == 1;

        LfsrNoise s;                                    // raw sequence mean
        s.prepare (48000.0);
        const long period = shortMode ? 93 : 32767;
        double rawSum = 0.0;
        for (long i = 0; i < period; ++i)
        {
            s.step (shortMode);
            rawSum += (s.state() & 1u) ? -1.0 : 1.0;
        }
        const double rawMean = rawSum / static_cast<double> (period);

        LfsrNoise n;                                    // blocked tick() mean
        n.prepare (48000.0);
        double sum = 0.0;
        const int N = 96000;                            // 2 s @ 48k
        for (int i = 0; i < N; ++i)
            sum += n.tick (shortMode);
        const double mean = sum / static_cast<double> (N);

        std::printf ("  %s-mode mean: raw sequence %+.4f -> DC-blocked output %+.6f\n",
                     shortMode ? "short" : "long", rawMean, mean);
        CHECK_MSG (std::abs (mean) < 0.01,
                   "%s-mode DC not blocked: mean %.4f", shortMode ? "short" : "long", mean);
        if (shortMode)
            CHECK_MSG (rawMean > 0.5,
                       "short-mode raw sequence changed (mean %.4f) — DC test is stale",
                       rawMean);
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
    // Phase 5: the whole FX block on. FX process on the same absolute
    // 16-sample control grid, so bit-exactness across block sizes must hold.
    p.crush_bits = 6; p.crush_down = 3; p.crush_mix = 0.7f;
    p.dly_time = 10; p.dly_fb = 0.5f; p.dly_pingpong = true; p.dly_mix = 0.4f;
    p.cave_size = 0.6f; p.cave_damp = 0.5f; p.cave_mix = 0.4f;

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
    p.crush_bits = 4; p.crush_down = 8; p.crush_mix = 1.0f;      // Phase 5:
    p.dly_fb = 0.9f; p.dly_pingpong = true; p.dly_mix = 0.5f;    // all FX on
    p.cave_size = 0.9f; p.cave_damp = 0.5f; p.cave_mix = 0.5f;
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
static double measureCpuRatio (double sr, int blockSize)
{
    // Worst realtime patch: everything on, 16 held notes x full 8-way unison
    // = 128 stacks (kMaxUnisonStacks raised 64 -> 128, Phase-2 amendment),
    // plus the entire Phase-5 FX block engaged.
    ParamSnapshot p;
    p.oscB_on = true; p.oscB_sync = true; p.sub_on = true; p.noise_on = true;
    p.uni_count = 8; p.poly_count = 16;
    p.lfo1_pwm = 0.4f; p.lfo2_amt = 0.5f;
    p.env1_s = 1.0f;
    p.crush_bits = 4; p.crush_down = 2; p.crush_mix = 1.0f;
    p.dly_fb = 0.9f; p.dly_pingpong = true; p.dly_mix = 0.5f;
    p.cave_size = 0.9f; p.cave_damp = 0.5f; p.cave_mix = 0.5f;

    BlockwaveEngine engine;
    engine.setParams (p);
    engine.prepare (sr, blockSize);
    for (int i = 0; i < 16; ++i)
        engine.noteOn (36 + i * 3, 0.9f);

    static float l[4096], r[4096];
    const int blocks = static_cast<int> (5.0 * sr / blockSize);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i)
        engine.process (l, r, blockSize);
    const auto t1 = std::chrono::steady_clock::now();

    const double renderSec = std::chrono::duration<double> (t1 - t0).count();
    const double audioSec  = blocks * static_cast<double> (blockSize) / sr;
    return renderSec / audioSec;
}

static void test_cpu_worst_case()
{
    std::printf ("[cpu_worst_case]\n");
    std::printf ("  16 voices x full 8-way unison (128 stacks), all sources + ALL FX on:\n");
    const double r1 = measureCpuRatio (44100.0, 128);
    std::printf ("  %.1f%% of one core @ 44.1k/128 (%.2f%% per voice)\n",
                 r1 * 100.0, r1 * 100.0 / 16.0);
    const double r2 = measureCpuRatio (48000.0, 512);
    std::printf ("  %.1f%% of one core @ 48k/512  (%.2f%% per voice)\n",
                 r2 * 100.0, r2 * 100.0 / 16.0);
    CHECK_MSG (r1 < 0.9, "worst case not realtime @ 44.1k/128: %.1f%%", r1 * 100.0);
    CHECK_MSG (r2 < 0.9, "worst case not realtime @ 48k/512: %.1f%%", r2 * 100.0);
    // Phase-5 FX budget: total must stay under ~3x the Phase-2 3.5% figure.
    // Only optimized builds are held to that number — an unoptimized build runs
    // this patch ~6x slower, so its timing is not a meaningful budget signal.
    // The threshold is what varies by config, not whether the CHECK runs: the
    // suite must report the same check count in every build, or a skipped
    // assertion is indistinguishable from a deleted one.
#ifdef NDEBUG
    const double fxBudget = 0.105;      // enforced FX budget
#else
    const double fxBudget = 0.9;        // Debug: realtime ceiling only
#endif
    CHECK_MSG (r1 < fxBudget, "FX budget blown @ 44.1k/128: %.1f%% > %.1f%%",
               r1 * 100.0, fxBudget * 100.0);
    CHECK_MSG (r2 < fxBudget, "FX budget blown @ 48k/512: %.1f%% > %.1f%%",
               r2 * 100.0, fxBudget * 100.0);
}

// ---------------------------------------------------------------------------
// Amendment 1: LFO2->pitch full scale ±12 st with quadratic taper
// (effective depth = amt^2 * 12 st, sign of amt preserved).
static void test_lfo2_pitch_taper()
{
    std::printf ("[lfo2_pitch_taper]\n");
    const double sr = 48000.0;
    const int note = 57;                                 // A3, 220 Hz
    const double base = 220.0;

    const float amts[]     = { 1.0f, 0.5f, -0.5f };
    const double expectSt[] = { 12.0, 3.0, -3.0 };       // amt*|amt|*12

    for (int k = 0; k < 3; ++k)
    {
        ParamSnapshot p;
        p.env1_s = 1.0f;
        p.lfo2_dest  = Lfo2Dest::pitch;
        p.lfo2_shape = LfoShape::square;
        p.lfo2_sync  = false;
        p.lfo2_rate  = 0.05f;        // 20 s period: square stays at +1 all render
        p.lfo2_amt   = amts[k];
        auto r = renderNote (p, note, sr, 1.5, 1.5);
        const double f0 = measureF0 (r.l, static_cast<int> (0.5 * sr),
                                     static_cast<int> (1.4 * sr), sr);
        const double ref = base * std::exp2 (expectSt[k] / 12.0);
        const double cents = centsDiff (f0, ref);
        std::printf ("  amt=%+.2f -> f0=%8.3f Hz, expected %8.3f Hz (%+.1f st), err %+.2f cents\n",
                     static_cast<double> (amts[k]), f0, ref, expectSt[k], cents);
        CHECK_MSG (std::abs (cents) <= 3.0,
                   "quadratic taper off: %+.2f cents at amt %.2f", cents,
                   static_cast<double> (amts[k]));
    }
}

// ---------------------------------------------------------------------------
// Amendment 3: pitch bend, fixed ±2 st, smoothed (no zipper).
static void test_pitch_bend()
{
    std::printf ("[pitch_bend]\n");
    const double sr = 48000.0;
    const int note = 57;                                 // A3, 220 Hz

    ParamSnapshot p;
    p.env1_s = 1.0f;

    BlockwaveEngine engine;
    engine.setParams (p);
    engine.prepare (sr, 512);
    engine.noteOn (note, 1.0f);

    const int nPre = static_cast<int> (1.0 * sr);
    const int nPost = static_cast<int> (1.5 * sr);
    std::vector<float> l (static_cast<size_t> (nPre + nPost)),
                       r (static_cast<size_t> (nPre + nPost));
    for (int pos = 0; pos < nPre; pos += 512)
        engine.process (l.data() + pos, r.data() + pos, std::min (512, nPre - pos));
    engine.setPitchBend (2.0f);
    for (int pos = nPre; pos < nPre + nPost; pos += 512)
        engine.process (l.data() + pos, r.data() + pos, std::min (512, nPre + nPost - pos));

    const double fPre = measureF0 (l, static_cast<int> (0.3 * sr),
                                   static_cast<int> (0.9 * sr), sr);
    const double fPost = measureF0 (l, nPre + static_cast<int> (0.5 * sr),
                                    nPre + static_cast<int> (1.4 * sr), sr);
    const double refPost = 220.0 * std::exp2 (2.0 / 12.0);
    std::printf ("  pre-bend %.3f Hz, post-bend %.3f Hz (target %.3f)\n", fPre, fPost, refPost);
    CHECK_MSG (std::abs (centsDiff (fPre, 220.0)) <= 1.0,
               "pre-bend pitch off: %.2f cents", centsDiff (fPre, 220.0));
    CHECK_MSG (std::abs (centsDiff (fPost, refPost)) <= 1.0,
               "post-bend pitch off: %.2f cents", centsDiff (fPost, refPost));

    // No zipper: successive waveform periods through the transition must move
    // gradually. 25 ms smoothing at 220 Hz gives < ~2% per period; a hard
    // step would jump the full 12.2% in one period.
    {
        std::vector<double> periods;
        double lastT = -1.0;
        for (int i = nPre - static_cast<int> (0.05 * sr) + 1;
             i < nPre + static_cast<int> (0.3 * sr); ++i)
        {
            const auto a = l[static_cast<size_t> (i - 1)], b = l[static_cast<size_t> (i)];
            if (a < 0.0f && b >= 0.0f)
            {
                const double t = (i - 1) + static_cast<double> (-a) / (b - a);
                if (lastT >= 0.0)
                    periods.push_back (t - lastT);
                lastT = t;
            }
        }
        double worstRatio = 1.0;
        for (size_t i = 1; i < periods.size(); ++i)
        {
            const double ratio = periods[i] > periods[i - 1]
                               ? periods[i] / periods[i - 1]
                               : periods[i - 1] / periods[i];
            worstRatio = std::max (worstRatio, ratio);
        }
        std::printf ("  worst period-to-period step through bend: %.2f%% (%zu periods)\n",
                     (worstRatio - 1.0) * 100.0, periods.size());
        CHECK_MSG (periods.size() > 40, "too few periods measured (%zu)", periods.size());
        CHECK_MSG (worstRatio < 1.04,
                   "bend zippered: %.2f%% period jump", (worstRatio - 1.0) * 100.0);
    }

    // Fixed range: out-of-range values clamp to ±2 st.
    engine.setPitchBend (10.0f);
    std::vector<float> l2 (static_cast<size_t> (sr)), r2 (static_cast<size_t> (sr));
    for (int pos = 0; pos < static_cast<int> (sr); pos += 512)
        engine.process (l2.data() + pos, r2.data() + pos,
                        std::min (512, static_cast<int> (sr) - pos));
    const double fClamp = measureF0 (l2, static_cast<int> (0.4 * sr),
                                     static_cast<int> (0.95 * sr), sr);
    std::printf ("  bend request +10 st -> clamped f0 %.3f Hz\n", fClamp);
    CHECK_MSG (std::abs (centsDiff (fClamp, refPost)) <= 1.0,
               "bend clamp failed: %.2f cents from +2 st", centsDiff (fClamp, refPost));
}

// ---------------------------------------------------------------------------
// Amendment 2: unison cap raised to 128 stacks — 16 voices x 8 unison is now
// genuinely possible (the old 64-stack cap silently folded it to 4-way).
static void test_unison_cap_128()
{
    std::printf ("[unison_cap_128]\n");
    static_assert (kMaxUnisonStacks == 128, "cap must be 128 (Phase-2 amendment)");
    static_assert (kMaxVoices * kMaxUnison == kMaxUnisonStacks,
                   "full 16x8 unison must fit the cap");

    ParamSnapshot p;
    p.uni_count = 8; p.poly_count = 16;
    p.uni_detune = 30.0f; p.env1_s = 1.0f;

    ParamSnapshot p4 = p;
    p4.uni_count = 4;

    const auto renderChord = [] (const ParamSnapshot& ps)
    {
        BlockwaveEngine e;
        e.setParams (ps);
        e.prepare (48000.0, 512);
        for (int i = 0; i < 16; ++i)
            e.noteOn (40 + i * 2, 0.9f);
        std::vector<float> l (24000), r (24000);
        for (int pos = 0; pos < 24000; pos += 512)
            e.process (l.data() + pos, r.data() + pos, std::min (512, 24000 - pos));
        return l;
    };

    const auto a = renderChord (p);      // 16 voices x 8 requested
    const auto b = renderChord (p4);     // 16 voices x 4 (the old capped result)
    double diff = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        diff += std::abs (static_cast<double> (a[i]) - b[i]);
    diff /= static_cast<double> (a.size());
    std::printf ("  mean |8-way - 4-way| with 16 voices = %.4f\n", diff);
    CHECK_MSG (diff > 1.0e-3,
               "16 voices x 8 unison identical to 4-way: cap still folding (%.3g)", diff);
}

// ---------------------------------------------------------------------------
// Amendment 4: master softclip ceiling — transparent below -0.3 dBFS, hard
// guarantee of < 0 dBFS above, always on.
static void test_master_softclip()
{
    std::printf ("[master_softclip]\n");
    constexpr float t = 0.96605088f;

    // Bit-exact identity below the threshold.
    {
        int mismatches = 0;
        for (int i = 0; i <= 2000; ++i)
        {
            const float x = -0.96f + 0.96f * static_cast<float> (i) / 1000.0f;
            if (masterSoftclip (x) != x)
                ++mismatches;
        }
        CHECK_MSG (mismatches == 0, "softclip not bit-transparent below threshold (%d)", mismatches);
    }
    // Bounded (<= 0 dBFS; the tanh asymptote rounds to exactly 1.0 in float
    // for extreme inputs, which the amendment allows) and monotonic above.
    {
        float prev = 0.0f;
        bool monotonic = true, bounded = true;
        for (int i = 0; i <= 1000; ++i)
        {
            const float y = masterSoftclip (10.0f * static_cast<float> (i) / 1000.0f);
            if (y < prev) monotonic = false;
            if (y > 1.0f) bounded = false;
            prev = y;
        }
        CHECK_MSG (monotonic, "softclip not monotonic");
        CHECK_MSG (bounded, "softclip exceeded 0 dBFS");
        CHECK_MSG (masterSoftclip (-10.0f) >= -1.0f, "negative rail exceeded");
    }

    // Hot resonant patch stays <= 0 dBFS.
    {
        ParamSnapshot p;
        p.uni_count = 8; p.uni_detune = 25.0f;
        p.oscA_level = 1.0f;
        p.filt_cutoff = 800.0f; p.filt_res = 0.9f;
        p.env1_s = 1.0f;
        p.master_gain = 6.0f;

        BlockwaveEngine e;
        e.setParams (p);
        e.prepare (48000.0, 512);
        for (int n : { 36, 43, 48, 55, 60 })
            e.noteOn (n, 1.0f);
        std::vector<float> l (48000), r (48000);
        for (int pos = 0; pos < 48000; pos += 512)
            e.process (l.data() + pos, r.data() + pos, std::min (512, 48000 - pos));
        float peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        std::printf ("  hot patch peak = %.6f (threshold %.5f)\n",
                     static_cast<double> (peak), static_cast<double> (t));
        CHECK_MSG (peak <= 1.0f, "hot patch exceeded 0 dBFS: %.4f", static_cast<double> (peak));
        CHECK_MSG (peak > t, "hot patch never engaged the clip — test patch too quiet");
    }

    // Quiet patch null: every sample below threshold, therefore the clip was
    // bit-transparent (verified by re-applying it — output must be identical).
    {
        ParamSnapshot p;                                 // defaults
        auto rn = renderNote (p, 60, 48000.0, 1.0, 1.2);
        float peak = 0.0f;
        int changed = 0;
        for (const float x : rn.l)
        {
            peak = std::max (peak, std::abs (x));
            if (masterSoftclip (x) != x)
                ++changed;
        }
        std::printf ("  quiet patch peak = %.4f, resamples changed by clip = %d\n",
                     static_cast<double> (peak), changed);
        CHECK_MSG (peak < t, "quiet patch reached the threshold (%.3f)", static_cast<double> (peak));
        CHECK_MSG (changed == 0, "quiet patch not null under softclip (%d samples)", changed);
    }
}

// ---------------------------------------------------------------------------
// Polyphony headroom (kVoiceHeadroom, chord-distortion investigation): a
// 4-note pad chord must not ride the master softclip into sustained
// intermodulation. Metric: nonlinearity residual — render the chord normally
// and again at -24 dB master (linear region), scale the quiet render back up;
// the chain is linear up to the softclip when the FX are off, so the relative
// RMS of the difference is exactly the clip's contribution. A hot negative
// control (+12 dB master) must blow the same gate, proving the metric bites.
static Rendered renderChord (const ParamSnapshot& p, const std::vector<int>& notes,
                             double sr, double noteSec, double totalSec)
{
    BlockwaveEngine engine;
    engine.setParams (p);
    engine.setTempo (120.0);
    engine.prepare (sr, 512);
    const int total = static_cast<int> (totalSec * sr);
    const int offAt = static_cast<int> (noteSec * sr);
    Rendered out;
    out.l.assign (static_cast<size_t> (total), 0.0f);
    out.r.assign (static_cast<size_t> (total), 0.0f);
    for (int n : notes)
        engine.noteOn (n, 100.0f / 127.0f);
    int pos = 0;
    bool offSent = false;
    while (pos < total)
    {
        if (! offSent && pos >= offAt)
        {
            for (int n : notes)
                engine.noteOff (n);
            offSent = true;
        }
        int n = std::min (512, total - pos);
        if (! offSent && pos + n > offAt)
            n = offAt - pos;
        engine.process (out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

static double chordClipResidualPct (const ParamSnapshot& p, const std::vector<int>& notes,
                                    double sr)
{
    const auto full = renderChord (p, notes, sr, 2.0, 2.5);
    ParamSnapshot quiet = p;
    quiet.master_gain = p.master_gain - 24.0f;
    const auto lin = renderChord (quiet, notes, sr, 2.0, 2.5);
    const double gain = std::pow (10.0, 24.0 / 20.0);
    double resid = 0.0, ref = 0.0;
    for (size_t i = 0; i < full.l.size(); ++i)
    {
        const double linUp = static_cast<double> (lin.l[i]) * gain;
        resid += (linUp - full.l[i]) * (linUp - full.l[i]);
        ref   += linUp * linUp;
    }
    return 100.0 * std::sqrt (resid / std::max (ref, 1.0e-12));
}

static void test_poly_chord_headroom()
{
    std::printf ("[poly_chord_headroom]\n");
    const double sr = 48000.0;

    // The dev PWM pad, replicated field-for-field from
    // presets/factory/DEV_PWM_PAD.json (the preset Kirill's chord report used).
    ParamSnapshot pad;
    pad.lfo1_pwm = 0.45f; pad.lfo1_sync = true; pad.lfo1_rate = 2.0f;
    pad.uni_count = 5; pad.uni_detune = 16.0f; pad.uni_spread = 0.9f;
    pad.filt_cutoff = 6500.0f;
    pad.env1_a = 0.5f; pad.env1_d = 0.5f; pad.env1_s = 0.8f; pad.env1_r = 1.2f;
    pad.vel_amp = 0.3f;

    const std::vector<int> chord4 { 48, 52, 55, 59 };   // C3 E3 G3 B3

    // Single-note sanity: healthy level, comfortably under the clip threshold.
    {
        const auto single = renderChord (pad, { 48 }, sr, 2.0, 2.5);
        float peak = 0.0f;
        for (const float v : single.l)
            peak = std::max (peak, std::abs (v));
        std::printf ("  single-note peak %.4f (want ~-6 dBFS: 0.2..0.9)\n",
                     static_cast<double> (peak));
        CHECK_MSG (peak > 0.2f && peak < 0.9f,
                   "single-note level not sane after the headroom trim: %.4f",
                   static_cast<double> (peak));
    }

    // 4-note chord: intermod/dirt gate. Pre-trim this measured 18.5%;
    // post-trim 0.5% — the 2% gate leaves margin without ever letting the
    // pre-trim behavior back in.
    const double resid4 = chordClipResidualPct (pad, chord4, sr);
    std::printf ("  4-note chord nonlinearity residual %.2f%% (gate 2%%)\n", resid4);
    CHECK_MSG (resid4 < 2.0, "pad chord rides the softclip: residual %.2f%%", resid4);

    // Negative control: the same chord pushed +12 dB into the ceiling must
    // trip the gate by a wide margin, or the metric proves nothing.
    ParamSnapshot hot = pad;
    hot.master_gain = 12.0f;
    const double residHot = chordClipResidualPct (hot, chord4, sr);
    std::printf ("  negative control (+12 dB master) residual %.2f%%\n", residHot);
    CHECK_MSG (residHot > 10.0,
               "clip-residual metric is toothless: hot chord scored %.2f%%", residHot);
}

// ---------------------------------------------------------------------------
// Phase-2 DoD: host automation of cutoff/PW is click-free (engine smoothing).
// A stepped parameter change mid-note must not add a discontinuity beyond the
// waveform's own steady-state slew; an unsmoothed switch (spliced renders)
// must trip the same detector — proving the detector actually bites.
static void test_param_smoothing_click_free()
{
    std::printf ("[param_smoothing_click_free]\n");
    const double sr = 48000.0;
    const int note = 33;                                 // A1, 55 Hz

    ParamSnapshot p1;
    p1.env1_a = 0.005f; p1.env1_s = 1.0f;
    p1.filt_cutoff = 400.0f; p1.filt_res = 0.1f;

    ParamSnapshot p2 = p1;
    p2.filt_cutoff = 1200.0f;
    p2.oscA_pw = 15.0f;

    const int total = static_cast<int> (1.2 * sr);
    const int stepAt = static_cast<int> (0.5 * sr);

    BlockwaveEngine e;
    e.setParams (p1);
    e.prepare (sr, 512);
    e.noteOn (note, 1.0f);
    std::vector<float> l (static_cast<size_t> (total)), r (static_cast<size_t> (total));
    bool stepped = false;
    for (int pos = 0; pos < total; )
    {
        if (! stepped && pos >= stepAt) { e.setParams (p2); stepped = true; }
        int n = std::min (512, total - pos);
        if (! stepped && pos + n > stepAt)
            n = stepAt - pos;
        e.process (l.data() + pos, r.data() + pos, n);
        pos += n;
    }

    const auto maxSlew = [&l] (int from, int to)
    {
        float m = 0.0f;
        for (int i = from + 1; i < to; ++i)
            m = std::max (m, std::abs (l[static_cast<size_t> (i)]
                                     - l[static_cast<size_t> (i - 1)]));
        return m;
    };

    const float steady1 = maxSlew (static_cast<int> (0.25 * sr), static_cast<int> (0.48 * sr));
    const float steady2 = maxSlew (static_cast<int> (0.90 * sr), static_cast<int> (1.15 * sr));
    const float trans   = maxSlew (stepAt, static_cast<int> (0.65 * sr));
    const float bound   = 1.25f * std::max (steady1, steady2);
    std::printf ("  slew: steady(before)=%.4f steady(after)=%.4f transition=%.4f bound=%.4f\n",
                 static_cast<double> (steady1), static_cast<double> (steady2),
                 static_cast<double> (trans), static_cast<double> (bound));
    CHECK_MSG (trans <= bound,
               "cutoff/PW step clicked: transition slew %.4f > bound %.4f",
               static_cast<double> (trans), static_cast<double> (bound));

    // Negative control: splice two independent steady renders at the step
    // point — that is what an unsmoothed parameter jump does to the output.
    {
        auto ra = renderNote (p1, note, sr, 1.2, 1.2);
        auto rb = renderNote (p2, note, sr, 1.2, 1.2);
        float spliceSlew = 0.0f;
        for (int k = 0; k < 10; ++k)
        {
            const size_t n0 = static_cast<size_t> (stepAt + k * 17);
            spliceSlew = std::max (spliceSlew, std::abs (rb.l[n0] - ra.l[n0 - 1]));
        }
        std::printf ("  negative control (hard splice) slew = %.4f\n",
                     static_cast<double> (spliceSlew));
        CHECK_MSG (spliceSlew > bound,
                   "click detector too lax: splice slew %.4f under bound %.4f",
                   static_cast<double> (spliceSlew), static_cast<double> (bound));
    }
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
    test_lfo2_pitch_taper();
    test_pitch_bend();
    test_unison_cap_128();
    test_master_softclip();
    test_poly_chord_headroom();
    test_param_smoothing_click_free();

    // Phase-5 FX block (tests/FxTests.h):
    fxtests::test_fx_bypass_null();
    fxtests::test_crush_quantize_and_hold();
    fxtests::test_delay_tempo_lock();
    fxtests::test_delay_tempo_click_free();
    fxtests::test_delay_pingpong();
    fxtests::test_cave_tail();
    fxtests::test_cave_damp_spectral();

    // Phase-4 CRAFT engine (tests/CraftTests.h):
    crafttests::runAll();

    // Phase-4 UI audio path (tests/UiAudioTests.h):
    uitests::runAll();

    test_cpu_worst_case();

    std::printf ("\n%d checks, %d failures\n", state().checks, state().failures);
    return state().failures == 0 ? 0 : 1;
}
