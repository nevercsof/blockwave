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

// SINGLE SOURCE OF TRUTH for the SPEC parameter table (docs/SPEC.md).
// The APVTS layout (plugin/BlockwaveApvts.h), the preset JSON mapping
// (tools/render/PresetMapping.h) and the preset saver are all generated from
// the table below, so the plugin and tools/render agree exactly.
//
// *** IDs, ranges, defaults and tapers are FROZEN after Phase 2. ***
// Never rename, reorder, retype or re-taper an entry (host automation
// stores normalized values). Appending new parameters at the end is the
// only allowed change.
//
// Note: this header needs juce_core only (NormalisableRange, var). The DSP
// engine itself (BlockwaveEngine.h and friends) stays JUCE-free.
//
// LFO rate semantics (frozen): lfo1_rate / lfo2_rate are a single float
// 0.01..40. When lfoN_sync is ON the value is the period in WHOLE NOTES
// (1.0 = 1/1, 0.25 = 1/4, 8.0 = 8/1, 0.03125 = 1/32); when OFF it is Hz.
// Both SPEC extremes (8/1 = 8.0 and 1/32 = 0.03125) fit the range; the
// engine consumes the value directly and the Phase-3 UI quantizes synced
// display to divisions. dly_time (FX, tempo-synced only in v1) is a choice.

#include <cmath>
#include <juce_core/juce_core.h>
#include "BlockwaveParams.h"

namespace blockwave
{

// Order matches docs/SPEC.md parameter table exactly. FROZEN.
enum class PId : int
{
    oscA_on = 0, oscB_on, sub_on, noise_on,
    oscA_oct, oscB_oct, oscA_semi, oscB_semi,
    oscA_fine, oscB_fine, oscA_pw, oscB_pw,
    oscA_level, oscB_level, oscB_sync,
    sub_oct, sub_level, noise_mode, noise_level,
    uni_count, uni_detune, uni_spread,
    voice_mode, poly_count, glide_time, glide_mode,
    filt_type, filt_cutoff, filt_res, filt_env, filt_keytrack,
    env1_a, env1_d, env1_s, env1_r,
    env2_a, env2_d, env2_s, env2_r, env2_pitch,
    lfo1_rate, lfo1_sync, lfo1_pwm,
    lfo2_rate, lfo2_sync, lfo2_shape, lfo2_amt, lfo2_dest,
    crush_bits, crush_down, crush_mix,
    dly_time, dly_fb, dly_pingpong, dly_mix,
    cave_size, cave_damp, cave_mix,
    vel_amp, raw, master_gain,
    count
};

constexpr int kNumParams = static_cast<int> (PId::count);   // 61

enum class PKind : int { boolean, integer, floating, choice };

struct ParamDef
{
    const char* id;            // SPEC ID — FROZEN
    PKind kind;
    float minValue;            // bool: 0/1; choice: 0..numChoices-1
    float maxValue;
    float defaultValue;
    float skewCentre;          // > 0 => log-style taper with this midpoint
    const char* const* choices;
    int numChoices;
};

// Canonical choice strings — also the preset-JSON representation (except
// sub_oct, which is saved as the number -1 / -2 per the SPEC table).
inline constexpr const char* kSubOctChoices[]    = { "-1", "-2" };
inline constexpr const char* kNoiseModeChoices[] = { "long", "short" };
inline constexpr const char* kVoiceModeChoices[] = { "poly", "mono", "legato" };
inline constexpr const char* kGlideModeChoices[] = { "always", "legato" };
inline constexpr const char* kFiltTypeChoices[]  = { "LP24", "LP12", "BP", "HP" };
inline constexpr const char* kLfoShapeChoices[]  = { "square", "tri", "s&h" };
inline constexpr const char* kLfo2DestChoices[]  = { "pitch", "cutoff", "pw", "vol" };
inline constexpr const char* kDlyTimeChoices[]   = { "1/1", "1/2D", "1/2", "1/4D", "1/4",
                                                     "1/8D", "1/8", "1/16D", "1/16",
                                                     "1/32D", "1/32" };

inline const ParamDef& paramDef (PId id) noexcept
{
    static const ParamDef defs[kNumParams] =
    {
        //  id             kind             min      max      def      skew   choices             n
        { "oscA_on",       PKind::boolean,  0.0f,    1.0f,    1.0f,    0.0f,  nullptr,            0 },
        { "oscB_on",       PKind::boolean,  0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "sub_on",        PKind::boolean,  0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "noise_on",      PKind::boolean,  0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "oscA_oct",      PKind::integer, -2.0f,    2.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "oscB_oct",      PKind::integer, -2.0f,    2.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "oscA_semi",     PKind::integer, -12.0f,   12.0f,   0.0f,    0.0f,  nullptr,            0 },
        { "oscB_semi",     PKind::integer, -12.0f,   12.0f,   0.0f,    0.0f,  nullptr,            0 },
        { "oscA_fine",     PKind::floating, -100.0f, 100.0f,  0.0f,    0.0f,  nullptr,            0 },
        { "oscB_fine",     PKind::floating, -100.0f, 100.0f,  0.0f,    0.0f,  nullptr,            0 },
        { "oscA_pw",       PKind::floating, 1.0f,    99.0f,   50.0f,   0.0f,  nullptr,            0 },
        { "oscB_pw",       PKind::floating, 1.0f,    99.0f,   50.0f,   0.0f,  nullptr,            0 },
        { "oscA_level",    PKind::floating, 0.0f,    1.0f,    0.8f,    0.0f,  nullptr,            0 },
        { "oscB_level",    PKind::floating, 0.0f,    1.0f,    0.8f,    0.0f,  nullptr,            0 },
        { "oscB_sync",     PKind::boolean,  0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "sub_oct",       PKind::choice,   0.0f,    1.0f,    0.0f,    0.0f,  kSubOctChoices,     2 },
        { "sub_level",     PKind::floating, 0.0f,    1.0f,    0.7f,    0.0f,  nullptr,            0 },
        { "noise_mode",    PKind::choice,   0.0f,    1.0f,    0.0f,    0.0f,  kNoiseModeChoices,  2 },
        { "noise_level",   PKind::floating, 0.0f,    1.0f,    0.5f,    0.0f,  nullptr,            0 },
        { "uni_count",     PKind::integer,  1.0f,    8.0f,    1.0f,    0.0f,  nullptr,            0 },
        { "uni_detune",    PKind::floating, 0.0f,    100.0f,  15.0f,   0.0f,  nullptr,            0 },
        { "uni_spread",    PKind::floating, 0.0f,    1.0f,    0.5f,    0.0f,  nullptr,            0 },
        { "voice_mode",    PKind::choice,   0.0f,    2.0f,    0.0f,    0.0f,  kVoiceModeChoices,  3 },
        { "poly_count",    PKind::integer,  1.0f,    16.0f,   8.0f,    0.0f,  nullptr,            0 },
        { "glide_time",    PKind::floating, 0.0f,    2.0f,    0.0f,    0.3f,  nullptr,            0 },
        { "glide_mode",    PKind::choice,   0.0f,    1.0f,    1.0f,    0.0f,  kGlideModeChoices,  2 },
        { "filt_type",     PKind::choice,   0.0f,    3.0f,    0.0f,    0.0f,  kFiltTypeChoices,   4 },
        { "filt_cutoff",   PKind::floating, 20.0f,   20000.0f, 20000.0f, 632.456f, nullptr,       0 },
        { "filt_res",      PKind::floating, 0.0f,    1.0f,    0.1f,    0.0f,  nullptr,            0 },
        { "filt_env",      PKind::floating, -1.0f,   1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "filt_keytrack", PKind::floating, 0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "env1_a",        PKind::floating, 0.0f,    5.0f,    0.003f,  0.5f,  nullptr,            0 },
        { "env1_d",        PKind::floating, 0.0f,    5.0f,    0.120f,  0.5f,  nullptr,            0 },
        { "env1_s",        PKind::floating, 0.0f,    1.0f,    0.8f,    0.0f,  nullptr,            0 },
        { "env1_r",        PKind::floating, 0.0f,    5.0f,    0.080f,  0.5f,  nullptr,            0 },
        { "env2_a",        PKind::floating, 0.0f,    5.0f,    0.003f,  0.5f,  nullptr,            0 },
        { "env2_d",        PKind::floating, 0.0f,    5.0f,    0.200f,  0.5f,  nullptr,            0 },
        { "env2_s",        PKind::floating, 0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "env2_r",        PKind::floating, 0.0f,    5.0f,    0.100f,  0.5f,  nullptr,            0 },
        { "env2_pitch",    PKind::floating, -48.0f,  48.0f,   0.0f,    0.0f,  nullptr,            0 },
        { "lfo1_rate",     PKind::floating, 0.01f,   40.0f,   1.0f,    0.6325f, nullptr,          0 },
        { "lfo1_sync",     PKind::boolean,  0.0f,    1.0f,    1.0f,    0.0f,  nullptr,            0 },
        { "lfo1_pwm",      PKind::floating, 0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "lfo2_rate",     PKind::floating, 0.01f,   40.0f,   0.25f,   0.6325f, nullptr,          0 },
        { "lfo2_sync",     PKind::boolean,  0.0f,    1.0f,    1.0f,    0.0f,  nullptr,            0 },
        { "lfo2_shape",    PKind::choice,   0.0f,    2.0f,    1.0f,    0.0f,  kLfoShapeChoices,   3 },
        { "lfo2_amt",      PKind::floating, -1.0f,   1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "lfo2_dest",     PKind::choice,   0.0f,    3.0f,    1.0f,    0.0f,  kLfo2DestChoices,   4 },
        { "crush_bits",    PKind::integer,  1.0f,    16.0f,   16.0f,   0.0f,  nullptr,            0 },
        { "crush_down",    PKind::integer,  1.0f,    64.0f,   1.0f,    0.0f,  nullptr,            0 },
        { "crush_mix",     PKind::floating, 0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "dly_time",      PKind::choice,   0.0f,    10.0f,   4.0f,    0.0f,  kDlyTimeChoices,    11 },
        { "dly_fb",        PKind::floating, 0.0f,    0.9f,    0.35f,   0.0f,  nullptr,            0 },
        { "dly_pingpong",  PKind::boolean,  0.0f,    1.0f,    1.0f,    0.0f,  nullptr,            0 },
        { "dly_mix",       PKind::floating, 0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "cave_size",     PKind::floating, 0.0f,    1.0f,    0.5f,    0.0f,  nullptr,            0 },
        { "cave_damp",     PKind::floating, 0.0f,    1.0f,    0.5f,    0.0f,  nullptr,            0 },
        { "cave_mix",      PKind::floating, 0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "vel_amp",       PKind::floating, 0.0f,    1.0f,    0.5f,    0.0f,  nullptr,            0 },
        { "raw",           PKind::boolean,  0.0f,    1.0f,    0.0f,    0.0f,  nullptr,            0 },
        { "master_gain",   PKind::floating, -60.0f,  6.0f,    0.0f,    0.0f,  nullptr,            0 },
    };
    return defs[static_cast<int> (id)];
}

inline juce::NormalisableRange<float> makeRange (const ParamDef& d)
{
    juce::NormalisableRange<float> r (d.minValue, d.maxValue);
    if (d.skewCentre > 0.0f)
        r.setSkewForCentre (d.skewCentre);
    return r;
}

inline bool findParam (const juce::String& id, PId& out) noexcept
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (id == paramDef (static_cast<PId> (i)).id)
        {
            out = static_cast<PId> (i);
            return true;
        }
    }
    return false;
}

// JSON value -> choice index. Strings match the canonical tables; numbers are
// accepted for sub_oct (-1/-2 per SPEC) and as a raw index elsewhere.
inline int choiceIndexFromVar (PId id, const juce::var& v)
{
    const auto& d = paramDef (id);
    if (v.isString())
    {
        const auto s = v.toString();
        for (int c = 0; c < d.numChoices; ++c)
            if (s == d.choices[c])
                return c;
        return static_cast<int> (d.defaultValue);
    }
    int n = static_cast<int> (v);
    if (id == PId::sub_oct)
        return n == -2 ? 1 : 0;
    if (n < 0) n = 0;
    if (n > d.numChoices - 1) n = d.numChoices - 1;
    return n;
}

// JSON value -> plain parameter value, clamped and quantized exactly the way
// the APVTS stores it (normalize -> denormalize round trip), so tools/render
// and the plugin agree on the resulting ParamSnapshot.
inline float plainFromVar (PId id, const juce::var& v)
{
    const auto& d = paramDef (id);
    switch (d.kind)
    {
        case PKind::boolean:
            return static_cast<bool> (v) ? 1.0f : 0.0f;
        case PKind::integer:
        {
            int n = static_cast<int> (v);
            if (n < static_cast<int> (d.minValue)) n = static_cast<int> (d.minValue);
            if (n > static_cast<int> (d.maxValue)) n = static_cast<int> (d.maxValue);
            return static_cast<float> (n);
        }
        case PKind::choice:
            return static_cast<float> (choiceIndexFromVar (id, v));
        case PKind::floating:
        default:
        {
            float f = static_cast<float> (static_cast<double> (v));
            if (f < d.minValue) f = d.minValue;
            if (f > d.maxValue) f = d.maxValue;
            const auto r = makeRange (d);
            return r.convertFrom0to1 (r.convertTo0to1 (f));
        }
    }
}

// Plain value -> preset-JSON value (canonical representation for saving).
inline juce::var varFromPlain (PId id, float plain)
{
    const auto& d = paramDef (id);
    switch (d.kind)
    {
        case PKind::boolean:
            return juce::var (plain >= 0.5f);
        case PKind::integer:
            return juce::var (static_cast<int> (std::lround (plain)));
        case PKind::choice:
        {
            int idx = static_cast<int> (std::lround (plain));
            if (idx < 0) idx = 0;
            if (idx > d.numChoices - 1) idx = d.numChoices - 1;
            if (id == PId::sub_oct)
                return juce::var (idx == 1 ? -2 : -1);
            return juce::var (juce::String (d.choices[idx]));
        }
        case PKind::floating:
        default:
            return juce::var (static_cast<double> (plain));
    }
}

// Plain value -> ParamSnapshot field. The ONLY place the ID->field mapping
// exists; used by both the preset JSON path and the APVTS audio-thread path.
// Complete since Phase 5 (FX fields included).
inline void applyToSnapshot (ParamSnapshot& p, PId id, float v) noexcept
{
    const int  iv = static_cast<int> (std::lround (v));
    const bool bv = v >= 0.5f;
    switch (id)
    {
        case PId::oscA_on:      p.oscA_on = bv; break;
        case PId::oscB_on:      p.oscB_on = bv; break;
        case PId::sub_on:       p.sub_on = bv; break;
        case PId::noise_on:     p.noise_on = bv; break;
        case PId::oscA_oct:     p.oscA_oct = iv; break;
        case PId::oscB_oct:     p.oscB_oct = iv; break;
        case PId::oscA_semi:    p.oscA_semi = iv; break;
        case PId::oscB_semi:    p.oscB_semi = iv; break;
        case PId::oscA_fine:    p.oscA_fine = v; break;
        case PId::oscB_fine:    p.oscB_fine = v; break;
        case PId::oscA_pw:      p.oscA_pw = v; break;
        case PId::oscB_pw:      p.oscB_pw = v; break;
        case PId::oscA_level:   p.oscA_level = v; break;
        case PId::oscB_level:   p.oscB_level = v; break;
        case PId::oscB_sync:    p.oscB_sync = bv; break;
        case PId::sub_oct:      p.sub_oct = iv == 1 ? -2 : -1; break;
        case PId::sub_level:    p.sub_level = v; break;
        case PId::noise_mode:   p.noise_mode = iv == 1 ? NoiseMode::shortMode
                                                       : NoiseMode::longMode; break;
        case PId::noise_level:  p.noise_level = v; break;
        case PId::uni_count:    p.uni_count = iv; break;
        case PId::uni_detune:   p.uni_detune = v; break;
        case PId::uni_spread:   p.uni_spread = v; break;
        case PId::voice_mode:   p.voice_mode = iv == 1 ? VoiceMode::mono
                                             : iv == 2 ? VoiceMode::legato
                                                       : VoiceMode::poly; break;
        case PId::poly_count:   p.poly_count = iv; break;
        case PId::glide_time:   p.glide_time = v; break;
        case PId::glide_mode:   p.glide_mode = iv == 0 ? GlideMode::always
                                                       : GlideMode::legato; break;
        case PId::filt_type:    p.filt_type = iv == 1 ? FilterType::lp12
                                            : iv == 2 ? FilterType::bp
                                            : iv == 3 ? FilterType::hp
                                                      : FilterType::lp24; break;
        case PId::filt_cutoff:  p.filt_cutoff = v; break;
        case PId::filt_res:     p.filt_res = v; break;
        case PId::filt_env:     p.filt_env = v; break;
        case PId::filt_keytrack: p.filt_keytrack = v; break;
        case PId::env1_a:       p.env1_a = v; break;
        case PId::env1_d:       p.env1_d = v; break;
        case PId::env1_s:       p.env1_s = v; break;
        case PId::env1_r:       p.env1_r = v; break;
        case PId::env2_a:       p.env2_a = v; break;
        case PId::env2_d:       p.env2_d = v; break;
        case PId::env2_s:       p.env2_s = v; break;
        case PId::env2_r:       p.env2_r = v; break;
        case PId::env2_pitch:   p.env2_pitch = v; break;
        case PId::lfo1_rate:    p.lfo1_rate = v; break;
        case PId::lfo1_sync:    p.lfo1_sync = bv; break;
        case PId::lfo1_pwm:     p.lfo1_pwm = v; break;
        case PId::lfo2_rate:    p.lfo2_rate = v; break;
        case PId::lfo2_sync:    p.lfo2_sync = bv; break;
        case PId::lfo2_shape:   p.lfo2_shape = iv == 0 ? LfoShape::square
                                             : iv == 2 ? LfoShape::sampleHold
                                                       : LfoShape::tri; break;
        case PId::lfo2_amt:     p.lfo2_amt = v; break;
        case PId::lfo2_dest:    p.lfo2_dest = iv == 0 ? Lfo2Dest::pitch
                                            : iv == 2 ? Lfo2Dest::pw
                                            : iv == 3 ? Lfo2Dest::vol
                                                      : Lfo2Dest::cutoff; break;
        case PId::vel_amp:      p.vel_amp = v; break;
        case PId::raw:          p.raw = bv; break;
        case PId::master_gain:  p.master_gain = v; break;

        // FX block (Phase 5):
        case PId::crush_bits:   p.crush_bits = iv; break;
        case PId::crush_down:   p.crush_down = iv; break;
        case PId::crush_mix:    p.crush_mix = v; break;
        case PId::dly_time:     p.dly_time = iv; break;    // kDlyTimeChoices index
        case PId::dly_fb:       p.dly_fb = v; break;
        case PId::dly_pingpong: p.dly_pingpong = bv; break;
        case PId::dly_mix:      p.dly_mix = v; break;
        case PId::cave_size:    p.cave_size = v; break;
        case PId::cave_damp:    p.cave_damp = v; break;
        case PId::cave_mix:     p.cave_mix = v; break;

        case PId::count:
        default:
            break;
    }
}

// ParamSnapshot field -> plain parameter value (the exact inverse of
// applyToSnapshot). Added in Phase 4 so a crafted snapshot can be written
// into the APVTS field-by-field. Choice fields return their index.
inline float snapshotToPlain (const ParamSnapshot& p, PId id) noexcept
{
    switch (id)
    {
        case PId::oscA_on:      return p.oscA_on ? 1.0f : 0.0f;
        case PId::oscB_on:      return p.oscB_on ? 1.0f : 0.0f;
        case PId::sub_on:       return p.sub_on ? 1.0f : 0.0f;
        case PId::noise_on:     return p.noise_on ? 1.0f : 0.0f;
        case PId::oscA_oct:     return static_cast<float> (p.oscA_oct);
        case PId::oscB_oct:     return static_cast<float> (p.oscB_oct);
        case PId::oscA_semi:    return static_cast<float> (p.oscA_semi);
        case PId::oscB_semi:    return static_cast<float> (p.oscB_semi);
        case PId::oscA_fine:    return p.oscA_fine;
        case PId::oscB_fine:    return p.oscB_fine;
        case PId::oscA_pw:      return p.oscA_pw;
        case PId::oscB_pw:      return p.oscB_pw;
        case PId::oscA_level:   return p.oscA_level;
        case PId::oscB_level:   return p.oscB_level;
        case PId::oscB_sync:    return p.oscB_sync ? 1.0f : 0.0f;
        case PId::sub_oct:      return p.sub_oct == -2 ? 1.0f : 0.0f;
        case PId::sub_level:    return p.sub_level;
        case PId::noise_mode:   return p.noise_mode == NoiseMode::shortMode ? 1.0f : 0.0f;
        case PId::noise_level:  return p.noise_level;
        case PId::uni_count:    return static_cast<float> (p.uni_count);
        case PId::uni_detune:   return p.uni_detune;
        case PId::uni_spread:   return p.uni_spread;
        case PId::voice_mode:   return static_cast<float> (static_cast<int> (p.voice_mode));
        case PId::poly_count:   return static_cast<float> (p.poly_count);
        case PId::glide_time:   return p.glide_time;
        case PId::glide_mode:   return static_cast<float> (static_cast<int> (p.glide_mode));
        case PId::filt_type:    return static_cast<float> (static_cast<int> (p.filt_type));
        case PId::filt_cutoff:  return p.filt_cutoff;
        case PId::filt_res:     return p.filt_res;
        case PId::filt_env:     return p.filt_env;
        case PId::filt_keytrack: return p.filt_keytrack;
        case PId::env1_a:       return p.env1_a;
        case PId::env1_d:       return p.env1_d;
        case PId::env1_s:       return p.env1_s;
        case PId::env1_r:       return p.env1_r;
        case PId::env2_a:       return p.env2_a;
        case PId::env2_d:       return p.env2_d;
        case PId::env2_s:       return p.env2_s;
        case PId::env2_r:       return p.env2_r;
        case PId::env2_pitch:   return p.env2_pitch;
        case PId::lfo1_rate:    return p.lfo1_rate;
        case PId::lfo1_sync:    return p.lfo1_sync ? 1.0f : 0.0f;
        case PId::lfo1_pwm:     return p.lfo1_pwm;
        case PId::lfo2_rate:    return p.lfo2_rate;
        case PId::lfo2_sync:    return p.lfo2_sync ? 1.0f : 0.0f;
        case PId::lfo2_shape:   return static_cast<float> (static_cast<int> (p.lfo2_shape));
        case PId::lfo2_amt:     return p.lfo2_amt;
        case PId::lfo2_dest:    return static_cast<float> (static_cast<int> (p.lfo2_dest));
        case PId::crush_bits:   return static_cast<float> (p.crush_bits);
        case PId::crush_down:   return static_cast<float> (p.crush_down);
        case PId::crush_mix:    return p.crush_mix;
        case PId::dly_time:     return static_cast<float> (p.dly_time);
        case PId::dly_fb:       return p.dly_fb;
        case PId::dly_pingpong: return p.dly_pingpong ? 1.0f : 0.0f;
        case PId::dly_mix:      return p.dly_mix;
        case PId::cave_size:    return p.cave_size;
        case PId::cave_damp:    return p.cave_damp;
        case PId::cave_mix:     return p.cave_mix;
        case PId::vel_amp:      return p.vel_amp;
        case PId::raw:          return p.raw ? 1.0f : 0.0f;
        case PId::master_gain:  return p.master_gain;
        case PId::count:
        default:                return 0.0f;
    }
}

} // namespace blockwave
