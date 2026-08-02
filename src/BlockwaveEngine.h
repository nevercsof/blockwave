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

#include <atomic>
#include <cmath>
#include "BlockwaveParams.h"
#include "Voice.h"
#include "Lfo.h"

namespace blockwave
{

// The whole Phase-1 synth engine. Pure C++ (no JUCE); the plugin wraps it.
//
// Threading contract:
//  - prepare()/reset() from a non-realtime thread while audio is stopped.
//  - setParams() from the message thread (lock-free seqlock hand-off).
//  - setTempo() from any thread (atomic).
//  - noteOn()/noteOff()/process() from the audio thread only. No allocation,
//    no locks, no I/O anywhere on that path.
//
// OSCILLATOR-COUNT CAP: uni_count applies per voice. The engine caps the
// TOTAL number of unison stacks at kMaxUnisonStacks (128):
// effectiveUnison = min(uni_count, max(1, 128 / poly_count)). Since
// 16 voices x 8 unison = 128 stacks, the full SPEC maximum is now possible
// (architect amendment, Phase 2; the cap was 64 in Phase 1). Worst case is
// 128 stacks x 2 = 256 pulse oscillators + 16 sub + 16 noise = 288
// oscillators, re-measured comfortably realtime in the Phase-2 checkpoint.
// The cap is deterministic and documented here — single source.
//
// MASTER SOFTCLIP: the engine chain ends in a fixed transparent softclip
// (masterSoftclip below) — bit-exact unity below -0.3 dBFS, tanh-shaped
// above, output strictly < 1.0 (0 dBFS). Always on, allocation-free.

// Transparent master ceiling. Identity for |x| <= t (bit-exact — quiet
// material passes a null test), C1-continuous tanh saturation above, output
// asymptote t + (1-t) = 1.0 so the engine never exceeds 0 dBFS.
inline float masterSoftclip (float x) noexcept
{
    constexpr float t = 0.96605088f;              // 10^(-0.3/20), -0.3 dBFS
    const float a = x < 0.0f ? -x : x;
    if (a <= t)
        return x;
    const float y = t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t));
    return x < 0.0f ? -y : y;
}

class BlockwaveEngine
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/) noexcept
    {
        sr = sampleRate;
        for (auto& v : voices)
            v.prepare (sampleRate);
        lfo1.prepare (sampleRate);
        lfo2.prepare (sampleRate);
        // ~25 ms one-pole smoothing; per-chunk alpha derived from the exact
        // chunk length so block-size splits are bit-identical.
        smoothLambda = -1.0f / (0.025f * static_cast<float> (sampleRate));
        store.read (params);
        snapSmoothers();
        reset();
    }

    void reset() noexcept
    {
        for (auto& v : voices)
            v.prepare (sr);
        lfo1.reset();
        lfo2.reset();
        heldCount = 0;
        ctrlPhase = 0;
        serialCounter = 0;
        lastNote = -1.0f;
        anyVoiceSounding = false;
        tBend = 0.0f;
        sBend = 0.0f;
        snapSmoothers();
    }

    // Message thread.
    void setParams (const ParamSnapshot& s) noexcept { store.set (s); }
    // Any thread.
    void setTempo (double bpm) noexcept { tempoBpm.store (bpm, std::memory_order_relaxed); }

    // ---- Audio thread from here on ----------------------------------------

    // MIDI pitch bend, fixed range ±2 st. Smoothed with the same ~25 ms
    // one-pole as the audible parameters, so wheel steps are zipper-free.
    void setPitchBend (float semitones) noexcept
    {
        tBend = semitones < -2.0f ? -2.0f : (semitones > 2.0f ? 2.0f : semitones);
    }

    void noteOn (int note, float velocity) noexcept
    {
        const bool poly = params.voice_mode == VoiceMode::poly;
        ++serialCounter;

        if (poly)
        {
            Voice* v = findVoiceToUse();
            const bool glideLegato = params.glide_mode == GlideMode::legato && anyVoiceSounding;
            const bool glideAlways = params.glide_mode == GlideMode::always && lastNote >= 0.0f;
            const float from = (glideLegato || glideAlways) && lastNote >= 0.0f
                             ? lastNote : static_cast<float> (note);
            v->noteOn (note, velocity, serialCounter, from, true);
        }
        else
        {
            pushHeld (note, velocity);
            Voice& v = voices[0];
            const bool overlapping = v.isActive() && ! v.isReleasing() && heldCount > 1;
            const bool legatoMode  = params.voice_mode == VoiceMode::legato;
            if (overlapping && legatoMode)
            {
                v.setTargetNote (note);          // no retrigger, glide applies
            }
            else
            {
                const bool glide = params.glide_mode == GlideMode::always
                                || (params.glide_mode == GlideMode::legato && overlapping);
                const float from = glide && lastNote >= 0.0f ? lastNote
                                                             : static_cast<float> (note);
                v.noteOn (note, velocity, serialCounter, from, true);
            }
        }
        lastNote = static_cast<float> (note);
        anyVoiceSounding = true;
    }

    void noteOff (int note) noexcept
    {
        if (params.voice_mode == VoiceMode::poly)
        {
            for (auto& v : voices)
                if (v.isActive() && ! v.isReleasing() && v.currentNote() == note)
                    v.noteOff();
        }
        else
        {
            removeHeld (note);
            Voice& v = voices[0];
            if (! v.isActive())
                return;
            if (v.currentNote() == note)
            {
                if (heldCount > 0)
                {
                    const auto& back = held[heldCount - 1];
                    if (params.voice_mode == VoiceMode::legato)
                        v.setTargetNote (back.note);
                    else
                        v.noteOn (back.note, back.velocity, ++serialCounter,
                                  lastNote >= 0.0f ? lastNote : static_cast<float> (back.note),
                                  true);
                    lastNote = static_cast<float> (back.note);
                }
                else
                {
                    v.noteOff();
                }
            }
        }
    }

    void allNotesOff() noexcept
    {
        for (auto& v : voices)
            if (v.isActive())
                v.noteOff();
        heldCount = 0;
    }

    // Overwrites outL/outR. Any numSamples >= 0 (internally chunked to 16).
    void process (float* outL, float* outR, int numSamples) noexcept
    {
        store.read (params);   // keeps previous copy on contention

        // Mode change hygiene: mono/legato use voice 0 only — release strays.
        if (params.voice_mode != VoiceMode::poly)
            for (int i = 1; i < kMaxVoices; ++i)
                if (voices[i].isActive() && ! voices[i].isReleasing())
                    voices[i].noteOff();

        const double bpm = tempoBpm.load (std::memory_order_relaxed);
        lfo1.setRate (params.lfo1_rate, params.lfo1_sync, bpm);
        lfo2.setRate (params.lfo2_rate, params.lfo2_sync, bpm);

        // Smoother targets.
        tLvlA = params.oscA_level; tLvlB = params.oscB_level;
        tLvlSub = params.sub_level; tLvlNoise = params.noise_level;
        tPwA = params.oscA_pw * 0.01f; tPwB = params.oscB_pw * 0.01f;
        tCutLog2 = log2f (params.filt_cutoff < 20.0f ? 20.0f : params.filt_cutoff);
        tMaster = params.master_gain <= -60.0f ? 0.0f
                : powf (10.0f, params.master_gain * 0.05f);

        // Oscillator-count cap (see class comment).
        const int polyLimit = params.voice_mode == VoiceMode::poly
                            ? clampi (params.poly_count, 1, kMaxVoices) : 1;
        int effUni = clampi (params.uni_count, 1, kMaxUnison);
        if (effUni * polyLimit > kMaxUnisonStacks)
            effUni = kMaxUnisonStacks / polyLimit > 0 ? kMaxUnisonStacks / polyLimit : 1;

        for (int i = 0; i < numSamples; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }

        int offset = 0;
        bool sounding = false;
        while (offset < numSamples)
        {
            // Never span an absolute control-grid boundary: renders stay
            // bit-identical for every block size (see Voice.h, kCtrlSamples).
            const int toBoundary = kCtrlSamples - ctrlPhase;
            const int remaining  = numSamples - offset;
            const int chunk = remaining > toBoundary ? toBoundary : remaining;
            const bool gridAligned = ctrlPhase == 0;
            // Advance smoothers one control step (exact for chunk length).
            const float alpha = 1.0f - std::exp (smoothLambda * static_cast<float> (chunk));
            sLvlA += alpha * (tLvlA - sLvlA);
            sLvlB += alpha * (tLvlB - sLvlB);
            sLvlSub += alpha * (tLvlSub - sLvlSub);
            sLvlNoise += alpha * (tLvlNoise - sLvlNoise);
            sPwA += alpha * (tPwA - sPwA);
            sPwB += alpha * (tPwB - sPwB);
            sCutLog2 += alpha * (tCutLog2 - sCutLog2);
            sMaster += alpha * (tMaster - sMaster);
            sBend += alpha * (tBend - sBend);

            float l1buf[kCtrlSamples], l2buf[kCtrlSamples];
            for (int i = 0; i < chunk; ++i)
            {
                l1buf[i] = lfo1.tick (LfoShape::tri);      // LFO1 is the PWM triangle
                l2buf[i] = lfo2.tick (params.lfo2_shape);
            }

            ModContext ctx;
            ctx.p = &params;
            ctx.updateControls = gridAligned;
            ctx.lvlA = sLvlA; ctx.lvlB = sLvlB;
            ctx.lvlSub = sLvlSub; ctx.lvlNoise = sLvlNoise;
            ctx.pwA = sPwA; ctx.pwB = sPwB;
            ctx.cutoffLog2 = sCutLog2;
            ctx.lfo1 = l1buf; ctx.lfo2 = l2buf;
            ctx.effectiveUnison = effUni;
            ctx.pitchBendSemis = sBend;

            for (auto& v : voices)
            {
                v.render (outL + offset, outR + offset, chunk, ctx);
                sounding = sounding || v.isActive();
            }

            for (int i = 0; i < chunk; ++i)
            {
                // End of chain: master gain, then the fixed softclip ceiling.
                outL[offset + i] = masterSoftclip (outL[offset + i] * sMaster);
                outR[offset + i] = masterSoftclip (outR[offset + i] * sMaster);
            }
            offset += chunk;
            ctrlPhase = (ctrlPhase + chunk) % kCtrlSamples;
        }
        anyVoiceSounding = sounding;
    }

    int activeVoiceCount() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            n += v.isActive() ? 1 : 0;
        return n;
    }

private:
    static int clampi (int v, int lo, int hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    Voice* findVoiceToUse() noexcept
    {
        const int polyLimit = clampi (params.poly_count, 1, kMaxVoices);

        int activeCount = 0;
        for (int i = 0; i < polyLimit; ++i)
            activeCount += voices[i].isActive() ? 1 : 0;

        if (activeCount < polyLimit)
            for (int i = 0; i < polyLimit; ++i)
                if (! voices[i].isActive())
                    return &voices[i];

        // Steal: oldest-quietest. Releasing voices first; then lowest ENV1
        // level; ties broken by lowest start serial (oldest).
        Voice* best = &voices[0];
        float bestScore = 1.0e30f;
        for (int i = 0; i < polyLimit; ++i)
        {
            Voice& v = voices[i];
            const float releasingBias = v.isReleasing() ? 0.0f : 100.0f;
            const float age = static_cast<float> (serialCounter - v.startSerial());
            const float score = releasingBias + v.envLevel() * 10.0f - age * 1.0e-4f;
            if (score < bestScore) { bestScore = score; best = &v; }
        }
        return best;
    }

    struct HeldNote { int note; float velocity; };

    void pushHeld (int note, float vel) noexcept
    {
        removeHeld (note);
        if (heldCount < kMaxVoices)
            held[heldCount++] = { note, vel };
    }

    void removeHeld (int note) noexcept
    {
        for (int i = 0; i < heldCount; ++i)
        {
            if (held[i].note == note)
            {
                for (int j = i; j < heldCount - 1; ++j)
                    held[j] = held[j + 1];
                --heldCount;
                return;
            }
        }
    }

    void snapSmoothers() noexcept
    {
        sLvlA = tLvlA = params.oscA_level;
        sLvlB = tLvlB = params.oscB_level;
        sLvlSub = tLvlSub = params.sub_level;
        sLvlNoise = tLvlNoise = params.noise_level;
        sPwA = tPwA = params.oscA_pw * 0.01f;
        sPwB = tPwB = params.oscB_pw * 0.01f;
        sCutLog2 = tCutLog2 = log2f (params.filt_cutoff < 20.0f ? 20.0f : params.filt_cutoff);
        sMaster = tMaster = params.master_gain <= -60.0f ? 0.0f
                          : powf (10.0f, params.master_gain * 0.05f);
    }

    double sr = 44100.0;
    ParamStore store;
    ParamSnapshot params;                 // audio-thread copy
    std::atomic<double> tempoBpm { 120.0 };

    Voice voices[kMaxVoices];
    Lfo lfo1, lfo2;

    float smoothLambda = -0.001f;
    float tBend = 0.0f, sBend = 0.0f;     // pitch bend target / smoothed, semitones
    float sLvlA = 0.8f, sLvlB = 0.8f, sLvlSub = 0.7f, sLvlNoise = 0.5f;
    float sPwA = 0.5f, sPwB = 0.5f, sCutLog2 = 14.3f, sMaster = 1.0f;
    float tLvlA = 0.8f, tLvlB = 0.8f, tLvlSub = 0.7f, tLvlNoise = 0.5f;
    float tPwA = 0.5f, tPwB = 0.5f, tCutLog2 = 14.3f, tMaster = 1.0f;

    HeldNote held[kMaxVoices] {};
    int heldCount = 0;
    int ctrlPhase = 0;
    std::uint32_t serialCounter = 0;
    float lastNote = -1.0f;
    bool anyVoiceSounding = false;
};

} // namespace blockwave
