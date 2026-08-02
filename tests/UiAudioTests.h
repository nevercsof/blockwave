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

// Phase-4 UI-audio path tests (pure C++ halves; the processor-level
// integration lives in tests/StateTests.cpp). Included by TestMain.cpp AFTER
// the AllocGuard definition so it can reuse it.
//
//  - UiMidiQueue: FIFO order, index wrap-around, capacity, documented
//    drop-newest + overflow-flag policy, one-shot overflow consumption
//  - DiscoveryJingle: exact silence when idle and after the last step, zero
//    first/last sample and a bounded fade ramp (click-free), peak parked at
//    -18 dBFS, the three arpeggio pitches correct at 44.1/48/96/192 kHz,
//    deterministic and finite (never stalls a render)
//  - the full audio-thread path (queue drain -> engine -> jingle mix) under
//    the global allocation guard

#include "UiMidiQueue.h"
#include "DiscoveryJingle.h"

namespace uitests
{

using namespace blockwave;
using namespace testutil;

// ---------------------------------------------------------------------------
static void test_ui_midi_queue()
{
    std::printf ("[ui_midi_queue]\n");
    UiMidiQueue q;
    UiMidiEvent e;

    CHECK_MSG (! q.pop (e), "fresh queue popped an event");
    CHECK_MSG (! q.consumeOverflow(), "fresh queue reports overflow");

    // Order and unsigned index wrap-around: far more traffic than the ring
    // holds, one in / one out, must stay in order forever.
    int mismatches = 0;
    for (int i = 0; i < 10000; ++i)
    {
        UiMidiEvent in;
        in.type = UiMidiEvent::noteOn;
        in.note = static_cast<std::uint8_t> (i % 128);
        in.velocity = static_cast<std::uint8_t> (1 + i % 127);
        if (! q.push (in))
            ++mismatches;
        UiMidiEvent out;
        if (! q.pop (out) || out.note != in.note || out.velocity != in.velocity
            || out.type != in.type)
            ++mismatches;
    }
    CHECK_MSG (mismatches == 0, "%d order/wrap failures over 10000 events", mismatches);
    CHECK_MSG (! q.consumeOverflow(), "one-in-one-out traffic flagged an overflow");

    // Capacity + overflow policy: exactly kCapacity accepted, the NEW event is
    // dropped (never the queued ones), overflow flag raised and one-shot.
    int accepted = 0;
    for (std::uint32_t i = 0; i < UiMidiQueue::kCapacity + 32; ++i)
    {
        UiMidiEvent in;
        in.type = UiMidiEvent::noteOn;
        in.note = static_cast<std::uint8_t> (i & 127u);
        in.velocity = 100;
        if (q.push (in))
            ++accepted;
    }
    CHECK_MSG (accepted == static_cast<int> (UiMidiQueue::kCapacity),
               "queue accepted %d events, capacity is %u", accepted,
               UiMidiQueue::kCapacity);
    CHECK_MSG (q.consumeOverflow(), "overflow flag not raised by a full queue");
    CHECK_MSG (! q.consumeOverflow(), "overflow flag is not one-shot");

    int drained = 0, badOrder = 0;
    while (q.pop (e))
    {
        if (e.note != static_cast<std::uint8_t> (drained & 127))
            ++badOrder;
        ++drained;
    }
    CHECK_MSG (drained == static_cast<int> (UiMidiQueue::kCapacity),
               "drained %d events, expected %u", drained, UiMidiQueue::kCapacity);
    CHECK_MSG (badOrder == 0, "%d events out of order after overflow", badOrder);

    std::printf ("  order, wrap-around, capacity %u, drop-newest overflow OK\n",
                 UiMidiQueue::kCapacity);
}

// ---------------------------------------------------------------------------
// Renders a triggered jingle plus a long silent tail.
static std::vector<float> renderJingle (double sr, int totalSamples)
{
    DiscoveryJingle j;
    j.prepare (sr);
    std::vector<float> out (static_cast<size_t> (totalSamples), 0.0f);
    j.start();
    for (int i = 0; i < totalSamples; ++i)
        out[static_cast<size_t> (i)] = j.tick();
    return out;
}

static void test_discovery_jingle()
{
    std::printf ("[discovery_jingle]\n");

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    for (double sr : rates)
    {
        // Idle before start(): must be exactly, bitwise zero.
        DiscoveryJingle idle;
        idle.prepare (sr);
        int idleNonZero = 0;
        for (int i = 0; i < 1000; ++i)
            if (idle.tick() != 0.0f)
                ++idleNonZero;
        CHECK_MSG (idleNonZero == 0, "%.0f Hz: untriggered jingle emitted %d non-zero samples",
                   sr, idleNonZero);
        CHECK_MSG (! idle.isActive(), "%.0f Hz: untriggered jingle claims to be active", sr);

        const int stepSamples = static_cast<int> (std::lround (
            DiscoveryJingle::kStepSeconds * sr));
        const int fadeSamples = static_cast<int> (std::lround (
            DiscoveryJingle::kFadeSeconds * sr));
        const int jingleLen = DiscoveryJingle::kNumSteps * stepSamples;
        const int total = jingleLen + static_cast<int> (0.2 * sr);
        const auto out = renderJingle (sr, total);

        // Click-free ends: first and last sample of the whole jingle exactly 0.
        CHECK_MSG (out[0] == 0.0f, "%.0f Hz: jingle starts at %.9f, not 0", sr,
                   static_cast<double> (out[0]));
        CHECK_MSG (out[static_cast<size_t> (jingleLen - 1)] == 0.0f,
                   "%.0f Hz: jingle ends at %.9f, not 0", sr,
                   static_cast<double> (out[static_cast<size_t> (jingleLen - 1)]));

        // Decays to silence: bitwise zero for the whole tail.
        int tailNonZero = 0;
        for (int i = jingleLen; i < total; ++i)
            if (out[static_cast<size_t> (i)] != 0.0f)
                ++tailNonZero;
        CHECK_MSG (tailNonZero == 0, "%.0f Hz: %d non-zero samples after the last step",
                   sr, tailNonZero);

        // Fade ramp: inside the attack of every step the envelope bounds the
        // sample (1.1x allows for the BLEP kernel's small overshoot).
        int fadeBreaches = 0;
        for (int s = 0; s < DiscoveryJingle::kNumSteps; ++s)
            for (int i = 0; i < fadeSamples; ++i)
            {
                const float bound = 1.1f * DiscoveryJingle::kPeak
                                  * static_cast<float> (i) / static_cast<float> (fadeSamples);
                if (std::abs (out[static_cast<size_t> (s * stepSamples + i)]) > bound + 1.0e-6f)
                    ++fadeBreaches;
            }
        CHECK_MSG (fadeBreaches == 0, "%.0f Hz: %d samples exceed the fade-in ramp",
                   sr, fadeBreaches);

        // Level: parked at -18 dBFS with headroom under the softclip ceiling.
        float peak = 0.0f;
        for (int i = 0; i < jingleLen; ++i)
            peak = std::max (peak, std::abs (out[static_cast<size_t> (i)]));
        CHECK_MSG (peak > 0.10f && peak < 0.16f,
                   "%.0f Hz: jingle peak %.4f is not near -18 dBFS", sr,
                   static_cast<double> (peak));

        // The three arpeggio pitches, measured in each step's sustain.
        for (int s = 0; s < DiscoveryJingle::kNumSteps; ++s)
        {
            const int a = s * stepSamples + fadeSamples + 8;
            const int b = (s + 1) * stepSamples - fadeSamples - 8;
            const double f0 = measureF0 (out, a, b, sr);
            const double ref = static_cast<double> (DiscoveryJingle::kStepHz[s]);
            CHECK_MSG (std::abs (centsDiff (f0, ref)) < 20.0,
                       "%.0f Hz: step %d measured %.2f Hz, expected %.2f Hz",
                       sr, s, f0, ref);
        }
    }

    // Deterministic and finite: two runs are bit-identical, and every sample
    // is finite (an offline render can never be stalled or poisoned by it).
    const auto a = renderJingle (48000.0, 20000);
    const auto b = renderJingle (48000.0, 20000);
    int diffs = 0, nonFinite = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i] != b[i]) ++diffs;
        if (! std::isfinite (a[i])) ++nonFinite;
    }
    CHECK_MSG (diffs == 0, "jingle is not deterministic (%d differing samples)", diffs);
    CHECK_MSG (nonFinite == 0, "%d non-finite jingle samples", nonFinite);

    // Re-trigger mid-flight restarts cleanly from silence.
    DiscoveryJingle j;
    j.prepare (48000.0);
    j.start();
    for (int i = 0; i < 1000; ++i)
        j.tick();
    j.start();
    CHECK_MSG (j.tick() == 0.0f, "re-trigger did not restart from a zero sample");

    std::printf ("  silence when idle, zero-ended fades, -18 dBFS, C6-G6-C7 at "
                 "44.1/48/96/192k OK\n");
}

// ---------------------------------------------------------------------------
// The exact audio-thread sequence PluginProcessor::processBlock runs, under
// the global allocation guard: drain the UI queue into the engine, render,
// then mix the jingle after the master softclip.
static void test_ui_path_no_allocation()
{
    std::printf ("[ui_path_no_allocation]\n");

    BlockwaveEngine engine;
    ParamSnapshot p;
    p.oscB_on = true; p.sub_on = true;
    p.poly_count = 16; p.uni_count = 4;
    engine.setParams (p);
    engine.prepare (48000.0, 512);

    UiMidiQueue queue;
    DiscoveryJingle jingle;
    jingle.prepare (48000.0);
    std::uint64_t held[2] { 0, 0 };

    static float l[512], r[512];

    const auto releaseHeld = [&]
    {
        for (int w = 0; w < 2; ++w)
        {
            if (held[w] == 0) continue;
            for (int b = 0; b < 64; ++b)
                if (((held[w] >> b) & 1u) != 0)
                    engine.noteOff (w * 64 + b);
            held[w] = 0;
        }
    };

    {
        AllocGuard guard;
        for (int block = 0; block < 200; ++block)
        {
            // Producer side (message thread in the plugin; SPSC correctness is
            // covered by test_ui_midi_queue).
            UiMidiEvent in;
            if (block % 5 == 0)
            {
                in.type = UiMidiEvent::noteOn;
                in.note = static_cast<std::uint8_t> (48 + block % 18);
                in.velocity = 100;
                queue.push (in);
            }
            if (block % 7 == 0)
            {
                in.type = UiMidiEvent::noteOff;
                in.note = static_cast<std::uint8_t> (48 + (block - 5) % 18);
                queue.push (in);
            }
            if (block == 120)
            {
                in.type = UiMidiEvent::allNotesOff;
                queue.push (in);
            }
            if (block == 150)                       // force the overflow path
                for (int i = 0; i < 200; ++i)
                {
                    in.type = UiMidiEvent::noteOn;
                    in.note = static_cast<std::uint8_t> (48 + i % 18);
                    in.velocity = 90;
                    queue.push (in);
                }

            // Consumer side — literally what drainUiMidi() does.
            UiMidiEvent e;
            while (queue.pop (e))
            {
                const int note = e.note;
                const int w = note >> 6, b = note & 63;
                const std::uint64_t bit = std::uint64_t (1) << b;
                if (e.type == UiMidiEvent::noteOn)
                {
                    engine.noteOn (note, static_cast<float> (e.velocity) / 127.0f);
                    held[w] |= bit;
                }
                else if (e.type == UiMidiEvent::noteOff)
                {
                    if ((held[w] & bit) != 0)
                    {
                        engine.noteOff (note);
                        held[w] &= ~bit;
                    }
                }
                else if (e.type == UiMidiEvent::allNotesOff)
                {
                    releaseHeld();
                }
            }
            if (queue.consumeOverflow())
                releaseHeld();

            engine.process (l, r, 512);

            if (block == 60)
                jingle.start();
            for (int i = 0; i < 512 && jingle.isActive(); ++i)
            {
                const float j = jingle.tick();
                l[i] = masterSoftclip (l[i] + j);
                r[i] = masterSoftclip (r[i] + j);
            }
        }
        CHECK_MSG (guard.count() == 0,
                   "UI MIDI + jingle audio path allocated %ld times", guard.count());
    }

    std::printf ("  0 allocations across 200 blocks with UI notes, overflow and "
                 "the jingle\n");
}

inline void runAll()
{
    test_ui_midi_queue();
    test_discovery_jingle();
    test_ui_path_no_allocation();
}

} // namespace uitests
