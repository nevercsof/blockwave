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

#include "TweakTab.h"

namespace blockwave::ui
{

BlockPanel& TweakTab::panel (const juce::String& title, int x, int y, int w,
                             const char* onParamId, const juce::String& onTooltip)
{
    auto p = std::make_unique<BlockPanel> (title);
    p->setBounds (x, y, w, kPanelH);
    addAndMakeVisible (*p);
    if (onParamId != nullptr)
    {
        auto& t = p->addTitleToggle (onTooltip);
        titleAtts.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, onParamId, t));
    }
    panels.push_back (std::move (p));
    return *panels.back();
}

ParamCell& TweakTab::cell (BlockPanel& p, PId pid, const juce::String& label,
                           const juce::String& tooltip)
{
    return p.addCell (std::make_unique<ParamCell> (state, pid, label, tooltip));
}

TweakTab::TweakTab (juce::AudioProcessorValueTreeState& s) : state (s)
{
    setSize (kCanvasW, kContentH);

    // Row 1 — sound sources. Panel x/w values are 8-px grid constants; the
    // whole 64-parameter table fits this canvas. Panels are exact-fit
    // (16 + 48*cells), so the three FX HP cells forced a row rebalance:
    // LFO1 joins row 2, MASTER ends row 4 after the FX chain — the only
    // 8-px-grid partition that keeps ENV1/ENV2 adjacent and FX in
    // signal order. Flagged in the checkpoint notes.
    auto& oscA = panel ("OSC A", 8, 8, 256, "oscA_on", "square voice one");
    cell (oscA, PId::oscA_oct,   "OCT");
    cell (oscA, PId::oscA_semi,  "SEMI");
    cell (oscA, PId::oscA_fine,  "FINE",  "cents of drift");
    cell (oscA, PId::oscA_pw,    "PW",    "square pulse width");
    cell (oscA, PId::oscA_level, "LEVEL");

    auto& oscB = panel ("OSC B", 272, 8, 304, "oscB_on", "square voice two");
    cell (oscB, PId::oscB_oct,   "OCT");
    cell (oscB, PId::oscB_semi,  "SEMI");
    cell (oscB, PId::oscB_fine,  "FINE",  "cents of drift");
    cell (oscB, PId::oscB_pw,    "PW",    "square pulse width");
    cell (oscB, PId::oscB_level, "LEVEL");
    cell (oscB, PId::oscB_sync,  "SYNC",  "hard sync A");

    auto& sub = panel ("SUB", 592, 8, 112, "sub_on", "square sub bass");
    cell (sub, PId::sub_oct,   "OCT");
    cell (sub, PId::sub_level, "LEVEL");

    auto& noise = panel ("NOISE", 712, 8, 112, "noise_on", "LFSR pulse noise");
    cell (noise, PId::noise_mode,  "MODE", "long or metallic");
    cell (noise, PId::noise_level, "LEVEL");

    // Row 2 — voice + filter + LFO1 (PWM).
    auto& voice = panel ("VOICE", 8, 104, 352);
    cell (voice, PId::uni_count,  "UNISON", "stacked square clones");
    cell (voice, PId::uni_detune, "DETUNE");
    cell (voice, PId::uni_spread, "SPREAD", "stereo block width");
    cell (voice, PId::voice_mode, "MODE");
    cell (voice, PId::poly_count, "VOICES");
    cell (voice, PId::glide_time, "GLIDE",  "note slide time");
    cell (voice, PId::glide_mode, "G.MODE");

    auto& filt = panel ("FILTER", 384, 104, 256);
    cell (filt, PId::filt_type,     "TYPE");
    cell (filt, PId::filt_cutoff,   "CUTOFF", "brightness cap");
    cell (filt, PId::filt_res,      "RES",    "filter squeal");
    cell (filt, PId::filt_env,      "ENVAMT", "env two depth");
    cell (filt, PId::filt_keytrack, "KEYTRK", "follows the keys");

    auto& lfo1 = panel ("LFO1 PWM", 664, 104, 160);
    lfo1RateCell = &cell (lfo1, PId::lfo1_rate, "RATE");
    cell (lfo1, PId::lfo1_sync, "SYNC", "locks to tempo");
    cell (lfo1, PId::lfo1_pwm,  "PWM",  "square width wobble");

    // Row 3 — envelopes + LFO2.
    auto& env1 = panel ("ENV1 AMP", 8, 200, 208);
    cell (env1, PId::env1_a, "ATTACK");
    cell (env1, PId::env1_d, "DECAY");
    cell (env1, PId::env1_s, "SUSTAIN");
    cell (env1, PId::env1_r, "RELEASE");

    auto& env2 = panel ("ENV2 MOD", 264, 200, 256);
    cell (env2, PId::env2_a, "ATTACK");
    cell (env2, PId::env2_d, "DECAY");
    cell (env2, PId::env2_s, "SUSTAIN");
    cell (env2, PId::env2_r, "RELEASE");
    cell (env2, PId::env2_pitch, "PITCH", "pitch drop blast");

    auto& lfo2 = panel ("LFO2", 568, 200, 256);
    lfo2RateCell = &cell (lfo2, PId::lfo2_rate, "RATE");
    cell (lfo2, PId::lfo2_sync,  "SYNC", "locks to tempo");
    cell (lfo2, PId::lfo2_shape, "SHAPE");
    cell (lfo2, PId::lfo2_amt,   "AMOUNT");
    cell (lfo2, PId::lfo2_dest,  "DEST", "what it wobbles");

    // Row 4 — FX chain in signal order, master gain at the end.
    auto& crush = panel ("CRUSH", 8, 296, 208);
    cell (crush, PId::crush_bits, "BITS", "bit depth dirt");
    cell (crush, PId::crush_down, "DOWN", "sample rate divide");
    cell (crush, PId::crush_hp,   "HP",   "trims crushed lows");
    cell (crush, PId::crush_mix,  "MIX");

    auto& dly = panel ("DELAY", 224, 296, 256);
    cell (dly, PId::dly_time,     "TIME", "tempo synced echo");
    cell (dly, PId::dly_fb,       "FEEDB");
    cell (dly, PId::dly_pingpong, "PING", "bounces left right");
    cell (dly, PId::dly_hp,       "HP",   "trims echo lows");
    cell (dly, PId::dly_mix,      "MIX");

    auto& cave = panel ("CAVE", 488, 296, 208);
    cell (cave, PId::cave_size, "SIZE", "cavern hugeness");
    cell (cave, PId::cave_damp, "DAMP", "darkens the tail");
    cell (cave, PId::cave_hp,   "HP",   "trims cave rumble");
    cell (cave, PId::cave_mix,  "MIX");

    auto& master = panel ("MASTER", 712, 296, 112);
    cell (master, PId::vel_amp,     "VELO", "velocity to volume");
    cell (master, PId::master_gain, "GAIN", "final output level");

    if (auto* p1 = state.getParameter ("lfo1_sync"))
        lfo1SyncWatch = std::make_unique<juce::ParameterAttachment> (
            *p1, [this] (float) { if (lfo1RateCell != nullptr) lfo1RateCell->refreshValue(); },
            nullptr);
    if (auto* p2 = state.getParameter ("lfo2_sync"))
        lfo2SyncWatch = std::make_unique<juce::ParameterAttachment> (
            *p2, [this] (float) { if (lfo2RateCell != nullptr) lfo2RateCell->refreshValue(); },
            nullptr);
}

} // namespace blockwave::ui
