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
#include <cstdint>
#include "BlockwaveParams.h"
#include "PolyBlepOsc.h"
#include "LfsrNoise.h"
#include "SvfFilter.h"
#include "AdsrEnv.h"

namespace blockwave
{

// Control rate: expensive updates (exp2, filter coefficients, glide) run once
// per kCtrlSamples on an ABSOLUTE sample grid maintained by the engine. The
// engine never hands a chunk that spans a grid boundary and sets
// ModContext::updateControls only on grid-aligned chunk starts, which makes
// renders bit-identical for every host block size (16..4096 and beyond).
constexpr int kCtrlSamples = 16;

// Fixed polyphony headroom: every voice is trimmed by -6 dB before it reaches
// the mix bus. Rationale (chord-distortion investigation, Phase-5 follow-up):
// a full-velocity voice can peak near 0 dBFS on its own, so a 3-4 note chord
// used to slam the master softclip into sustained saturation (14-18% nonlinear
// residual on the dev PWM pad — audible intermodulation "dirt"). With -6 dB a
// worst-case single note peaks around -6 dBFS and a 4-note pad chord's
// nonlinearity residual drops to ~0.5%; the softclip returns to being a
// safety ceiling instead of a permanently-engaged limiter. Deliberately a
// FIXED constant — no auto-gain, no pumping, renders stay deterministic.
constexpr float kVoiceHeadroom = 0.5f;    // -6.02 dB

struct ModContext
{
    const ParamSnapshot* p = nullptr;
    bool updateControls = true;        // chunk starts on the control grid
    // Smoothed (current) values, sampled by voices only on control updates:
    float lvlA = 0.0f, lvlB = 0.0f, lvlSub = 0.0f, lvlNoise = 0.0f;
    float pwA = 0.5f, pwB = 0.5f;      // 0..1
    float cutoffLog2 = 14.0f;          // log2(Hz)
    // ENV2 -> cutoff depth, -1..+1. Smoothed by the engine for the same
    // reason cutoffLog2 is: it is a multiplier on a log-frequency term
    // (+-5 octaves at full scale), so a stepped change moves the effective
    // cutoff by octaves in a single control step. See BlockwaveEngine.h.
    float filtEnv = 0.0f;
    // Per-sample LFO values for this chunk (bipolar -1..+1):
    const float* lfo1 = nullptr;
    const float* lfo2 = nullptr;
    int effectiveUnison = 1;           // engine-capped, 1..8
    float pitchBendSemis = 0.0f;       // smoothed MIDI pitch bend, fixed ±2 st
};

// One logical voice: up to 8 unison stacks of (OSC A + hard-synced OSC B),
// one SUB, one LFSR noise, a stereo SVF pair, ENV1 (amp) and ENV2 (mod).
// Unison stacks share this voice's envelopes and filter.
class Voice
{
public:
    // The engine assigns each pool slot its index once (prepare); the index
    // selects this voice's LFSR decorrelation seed (see LfsrNoise.h).
    void setVoiceIndex (int idx) noexcept { voiceIndex = idx; }

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        for (auto& s : stacks) { s.a.reset(); s.b.reset(); }
        subOsc.reset();
        noise.prepare (sampleRate);
        svfL.prepare (sampleRate);
        svfR.prepare (sampleRate);
        env1.prepare (sampleRate);
        env2.prepare (sampleRate);
        active = false;
        needsControlUpdate = true;
        noiseResetPending = false;
    }

    void noteOn (int midiNote, float vel, std::uint32_t serialIn,
                 float glideStartNote, bool retrigger) noexcept
    {
        note       = midiNote;
        targetNote = static_cast<float> (midiNote);
        velocity   = vel;
        serial     = serialIn;

        if (! active || retrigger)
        {
            glideNote = glideStartNote;
            if (! active)
            {
                // Fresh voice: clean state; slight fixed per-stack phase
                // offsets avoid a perfect comb on unison attack.
                for (int i = 0; i < kMaxUnison; ++i)
                {
                    const double ph = i * 0.11;
                    stacks[i].a.reset (ph - std::floor (ph));
                    stacks[i].b.reset (ph - std::floor (ph));
                }
                subOsc.reset();
                // Noise reset is deferred to the first control update: the
                // decorrelation seed depends on noise_mode, which lives in the
                // ParamSnapshot the render call carries. No noise sample is
                // produced before that update runs (needsControlUpdate is set
                // below), so the timing is identical to resetting here.
                noiseResetPending = true;
                svfL.reset();
                svfR.reset();
            }
            env1.noteOn();   // departs from current level -> click-free retrigger
            env2.noteOn();
        }
        active = true;
        needsControlUpdate = true;
    }

    void setTargetNote (int midiNote) noexcept   // legato transition
    {
        note = midiNote;
        targetNote = static_cast<float> (midiNote);
    }

    void noteOff() noexcept { env1.noteOff(); env2.noteOff(); }

    bool  isActive() const noexcept    { return active; }
    bool  isReleasing() const noexcept { return env1.isReleasing(); }
    int   currentNote() const noexcept { return note; }
    float envLevel() const noexcept    { return env1.current(); }
    std::uint32_t startSerial() const noexcept { return serial; }

    // Accumulates into outL/outR. numSamples <= kCtrlSamples and the chunk
    // never spans a control-grid boundary (engine contract).
    void render (float* outL, float* outR, int numSamples, const ModContext& ctx) noexcept
    {
        if (! active)
            return;

        if (ctx.updateControls || needsControlUpdate)
        {
            updateControls (ctx);
            needsControlUpdate = false;
        }

        const ParamSnapshot& p = *ctx.p;
        const bool shortNoise = p.noise_mode == NoiseMode::shortMode;

        for (int n = 0; n < numSamples; ++n)
        {
            const float e1 = env1.tick();
            env2.tick();

            const float l1 = ctx.lfo1 != nullptr ? ctx.lfo1[n] : 0.0f;
            const float l2 = ctx.lfo2 != nullptr ? ctx.lfo2[n] : 0.0f;
            float pwOff = p.lfo1_pwm * 0.49f * l1;
            if (p.lfo2_dest == Lfo2Dest::pw)
                pwOff += 0.49f * p.lfo2_amt * l2;
            const float pwA = clampf (cPwA + pwOff, 0.01f, 0.99f);
            const float pwB = clampf (cPwB + pwOff, 0.01f, 0.99f);

            float l = 0.0f, r = 0.0f;
            for (int i = 0; i < cUni; ++i)
            {
                auto& st = stacks[i];
                const auto ta = st.a.tick (cIncA[i], pwA, p.raw);
                float s = 0.0f;
                if (p.oscA_on)
                    s += ta.sample * cLvlA;
                if (p.oscB_on)
                {
                    const float syncFrac = (p.oscB_sync && ta.wrapped) ? ta.wrapFrac : -1.0f;
                    const auto tb = st.b.tick (cIncB[i], pwB, p.raw, syncFrac);
                    s += tb.sample * cLvlB;
                }
                l += s * cPanL[i];
                r += s * cPanR[i];
            }
            l *= cUniNorm;
            r *= cUniNorm;

            float centre = 0.0f;
            if (p.sub_on)
                centre += subOsc.tick (cSubInc, 0.5f, p.raw).sample * cLvlSub;
            if (p.noise_on)
                centre += noise.tick (shortNoise) * cLvlNoise;
            centre *= 0.70710678f;
            l += centre;
            r += centre;

            const float vca = e1 * cVelGain * cVolMod * kVoiceHeadroom;
            outL[n] += svfL.process (l) * vca;
            outR[n] += svfR.process (r) * vca;
        }

        if (! env1.isActive())
            active = false;
    }

private:
    void updateControls (const ModContext& ctx) noexcept
    {
        const ParamSnapshot& p = *ctx.p;

        if (noiseResetPending)
        {
            noise.resetForVoice (voiceIndex, p.noise_mode == NoiseMode::shortMode);
            noiseResetPending = false;
        }

        env1.setTimes (p.env1_a, p.env1_d, p.env1_s, p.env1_r);
        env2.setTimes (p.env2_a, p.env2_d, p.env2_s, p.env2_r);

        // Glide: exponential approach in semitone (log-frequency) domain,
        // advanced one fixed control step per update.
        if (p.glide_time > 1.0e-4f)
        {
            const float tau  = p.glide_time * 0.25f;   // ~98% travelled in glide_time
            const float coef = 1.0f - std::exp (-static_cast<float> (kCtrlSamples)
                                                / (tau * static_cast<float> (sr)));
            glideNote += coef * (targetNote - glideNote);
        }
        else
        {
            glideNote = targetNote;
        }

        const float e2 = env2.current();
        const float lfo2Start = ctx.lfo2 != nullptr ? ctx.lfo2[0] : 0.0f;

        float pitchMod = p.env2_pitch * e2;
        float cutMod   = 0.0f;                         // octaves
        cVolMod = 1.0f;
        switch (p.lfo2_dest)
        {
            // Full scale ±12 st with quadratic taper: effective depth is
            // amt² · 12 st, sign of amt preserved (amt·|amt| = sign(amt)·amt²).
            // Fine vibrato lives in the lower half of the knob; the top
            // reaches a full octave (architect amendment, Phase 2).
            case Lfo2Dest::pitch:
                pitchMod += 12.0f * p.lfo2_amt
                          * (p.lfo2_amt < 0.0f ? -p.lfo2_amt : p.lfo2_amt) * lfo2Start;
                break;
            case Lfo2Dest::cutoff: cutMod   += 4.0f * p.lfo2_amt * lfo2Start; break;
            case Lfo2Dest::vol:    cVolMod   = clampf (1.0f + 0.5f * p.lfo2_amt * lfo2Start, 0.0f, 2.0f); break;
            case Lfo2Dest::pw:
            default: break;                            // per-sample above
        }

        const float baseNote = glideNote + pitchMod + ctx.pitchBendSemis;
        const float baseHz   = 440.0f * exp2f ((baseNote - 69.0f) / 12.0f);

        const float cutLog2 = ctx.cutoffLog2
                            + 5.0f * ctx.filtEnv * e2
                            + cutMod
                            + p.filt_keytrack * (glideNote - 60.0f) / 12.0f;
        const float cutoffHz = exp2f (clampf (cutLog2, 4.32f, 14.4f));  // ~20 Hz..21.6 kHz
        svfL.setParams (p.filt_type, cutoffHz, p.filt_res);
        svfR.setParams (p.filt_type, cutoffHz, p.filt_res);

        cUni = ctx.effectiveUnison < 1 ? 1
             : (ctx.effectiveUnison > kMaxUnison ? kMaxUnison : ctx.effectiveUnison);
        cUniNorm = 1.0f / std::sqrt (static_cast<float> (cUni));
        const float offA = static_cast<float> (p.oscA_oct * 12 + p.oscA_semi) + p.oscA_fine * 0.01f;
        const float offB = static_cast<float> (p.oscB_oct * 12 + p.oscB_semi) + p.oscB_fine * 0.01f;
        for (int i = 0; i < cUni; ++i)
        {
            const float spreadPos = cUni == 1 ? 0.0f
                : 2.0f * static_cast<float> (i) / static_cast<float> (cUni - 1) - 1.0f;
            const float detuneSemis = spreadPos * p.uni_detune * 0.005f;  // ±detune/2 cents
            cIncA[i] = clampInc (baseHz * exp2f ((offA + detuneSemis) / 12.0f) / sr);
            cIncB[i] = clampInc (baseHz * exp2f ((offB + detuneSemis) / 12.0f) / sr);
            const float pan = spreadPos * p.uni_spread;
            cPanL[i] = std::sqrt (0.5f * (1.0f - pan));
            cPanR[i] = std::sqrt (0.5f * (1.0f + pan));
        }
        cSubInc = clampInc (baseHz * exp2f (static_cast<float> (p.sub_oct)) / sr);

        cLvlA = ctx.lvlA; cLvlB = ctx.lvlB;
        cLvlSub = ctx.lvlSub; cLvlNoise = ctx.lvlNoise;
        cPwA = ctx.pwA; cPwB = ctx.pwB;
        cVelGain = 1.0f - p.vel_amp * (1.0f - velocity);
    }

    static float clampf (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static double clampInc (double inc) noexcept
    {
        return inc < 0.0 ? 0.0 : (inc > 0.45 ? 0.45 : inc);
    }

    struct Stack { PolyBlepOsc a, b; };

    double sr = 44100.0;
    Stack stacks[kMaxUnison];
    PolyBlepOsc subOsc;
    LfsrNoise noise;
    SvfFilter svfL, svfR;
    AdsrEnv env1, env2;

    int   note = -1;
    float targetNote = 60.0f, glideNote = 60.0f;
    float velocity = 1.0f;
    std::uint32_t serial = 0;
    bool  active = false;
    bool  needsControlUpdate = true;
    bool  noiseResetPending = false;
    int   voiceIndex = 0;

    // Control-rate cache (valid between grid updates):
    double cIncA[kMaxUnison] {}, cIncB[kMaxUnison] {};
    float  cPanL[kMaxUnison] {}, cPanR[kMaxUnison] {};
    double cSubInc = 0.0;
    int    cUni = 1;
    float  cUniNorm = 1.0f;
    float  cLvlA = 0.0f, cLvlB = 0.0f, cLvlSub = 0.0f, cLvlNoise = 0.0f;
    float  cPwA = 0.5f, cPwB = 0.5f;
    float  cVelGain = 1.0f, cVolMod = 1.0f;
};

} // namespace blockwave
