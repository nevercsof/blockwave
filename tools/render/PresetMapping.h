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

// Maps SPEC parameter IDs from a preset JSON "params" object onto a
// ParamSnapshot. Phase 2 will reuse these exact IDs for the APVTS; keep the
// strings in lockstep with docs/SPEC.md (IDs are permanent after Phase 2).
// FX parameters (crush/dly/cave) are accepted but ignored until Phase 5.

#include <juce_core/juce_core.h>
#include "BlockwaveParams.h"

namespace blockwave
{

inline bool applyPresetParams (const juce::var& presetRoot, ParamSnapshot& p, juce::String& error)
{
    auto* obj = presetRoot.getDynamicObject();
    if (obj == nullptr) { error = "preset root is not an object"; return false; }

    const auto paramsVar = obj->getProperty ("params");
    if (paramsVar.isVoid())
        return true;                         // no overrides: SPEC defaults
    auto* params = paramsVar.getDynamicObject();
    if (params == nullptr) { error = "\"params\" is not an object"; return false; }

    const auto f = [] (const juce::var& v) { return static_cast<float> (double (v)); };
    const auto i = [] (const juce::var& v) { return static_cast<int> (v); };
    const auto b = [] (const juce::var& v) { return static_cast<bool> (v); };

    for (const auto& prop : params->getProperties())
    {
        const auto id = prop.name.toString();
        const auto& v = prop.value;

        if      (id == "oscA_on")     p.oscA_on = b (v);
        else if (id == "oscB_on")     p.oscB_on = b (v);
        else if (id == "sub_on")      p.sub_on = b (v);
        else if (id == "noise_on")    p.noise_on = b (v);
        else if (id == "oscA_oct")    p.oscA_oct = i (v);
        else if (id == "oscB_oct")    p.oscB_oct = i (v);
        else if (id == "oscA_semi")   p.oscA_semi = i (v);
        else if (id == "oscB_semi")   p.oscB_semi = i (v);
        else if (id == "oscA_fine")   p.oscA_fine = f (v);
        else if (id == "oscB_fine")   p.oscB_fine = f (v);
        else if (id == "oscA_pw")     p.oscA_pw = f (v);
        else if (id == "oscB_pw")     p.oscB_pw = f (v);
        else if (id == "oscA_level")  p.oscA_level = f (v);
        else if (id == "oscB_level")  p.oscB_level = f (v);
        else if (id == "oscB_sync")   p.oscB_sync = b (v);
        else if (id == "sub_oct")     p.sub_oct = i (v);
        else if (id == "sub_level")   p.sub_level = f (v);
        else if (id == "noise_mode")  p.noise_mode = v.toString() == "short" ? NoiseMode::shortMode : NoiseMode::longMode;
        else if (id == "noise_level") p.noise_level = f (v);
        else if (id == "uni_count")   p.uni_count = i (v);
        else if (id == "uni_detune")  p.uni_detune = f (v);
        else if (id == "uni_spread")  p.uni_spread = f (v);
        else if (id == "voice_mode")  p.voice_mode = v.toString() == "mono" ? VoiceMode::mono
                                                  : v.toString() == "legato" ? VoiceMode::legato : VoiceMode::poly;
        else if (id == "poly_count")  p.poly_count = i (v);
        else if (id == "glide_time")  p.glide_time = f (v);
        else if (id == "glide_mode")  p.glide_mode = v.toString() == "always" ? GlideMode::always : GlideMode::legato;
        else if (id == "filt_type")   p.filt_type = v.toString() == "LP12" ? FilterType::lp12
                                                 : v.toString() == "BP"   ? FilterType::bp
                                                 : v.toString() == "HP"   ? FilterType::hp : FilterType::lp24;
        else if (id == "filt_cutoff")   p.filt_cutoff = f (v);
        else if (id == "filt_res")      p.filt_res = f (v);
        else if (id == "filt_env")      p.filt_env = f (v);
        else if (id == "filt_keytrack") p.filt_keytrack = f (v);
        else if (id == "env1_a")      p.env1_a = f (v);
        else if (id == "env1_d")      p.env1_d = f (v);
        else if (id == "env1_s")      p.env1_s = f (v);
        else if (id == "env1_r")      p.env1_r = f (v);
        else if (id == "env2_a")      p.env2_a = f (v);
        else if (id == "env2_d")      p.env2_d = f (v);
        else if (id == "env2_s")      p.env2_s = f (v);
        else if (id == "env2_r")      p.env2_r = f (v);
        else if (id == "env2_pitch")  p.env2_pitch = f (v);
        else if (id == "lfo1_rate")   p.lfo1_rate = f (v);
        else if (id == "lfo1_sync")   p.lfo1_sync = b (v);
        else if (id == "lfo1_pwm")    p.lfo1_pwm = f (v);
        else if (id == "lfo2_rate")   p.lfo2_rate = f (v);
        else if (id == "lfo2_sync")   p.lfo2_sync = b (v);
        else if (id == "lfo2_shape")  p.lfo2_shape = v.toString() == "square" ? LfoShape::square
                                                  : v.toString() == "s&h"    ? LfoShape::sampleHold : LfoShape::tri;
        else if (id == "lfo2_amt")    p.lfo2_amt = f (v);
        else if (id == "lfo2_dest")   p.lfo2_dest = v.toString() == "pitch" ? Lfo2Dest::pitch
                                                  : v.toString() == "pw"    ? Lfo2Dest::pw
                                                  : v.toString() == "vol"   ? Lfo2Dest::vol : Lfo2Dest::cutoff;
        else if (id == "vel_amp")     p.vel_amp = f (v);
        else if (id == "raw")         p.raw = b (v);
        else if (id == "master_gain") p.master_gain = f (v);
        else if (id.startsWith ("crush_") || id.startsWith ("dly_") || id.startsWith ("cave_"))
            continue;                        // FX land in Phase 5
        // Unknown IDs are ignored (forward compatibility).
    }
    return true;
}

} // namespace blockwave
