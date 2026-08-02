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

// APVTS glue generated from the frozen SPEC table in src/ParamSpec.h:
//  - createParameterLayout(): every SPEC parameter, exact IDs/ranges/defaults.
//  - RawParams: cached std::atomic<float>* for the audio thread; toSnapshot()
//    builds a ParamSnapshot with zero allocation, locks or string work.
//  - preset JSON <-> APVTS (message thread only).

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamSpec.h"

namespace blockwave
{

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDef (static_cast<PId> (i));
        const juce::ParameterID pid (d.id, 1);      // version hint 1, permanent
        switch (d.kind)
        {
            case PKind::boolean:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    pid, d.id, d.defaultValue >= 0.5f));
                break;
            case PKind::integer:
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    pid, d.id, static_cast<int> (d.minValue),
                    static_cast<int> (d.maxValue), static_cast<int> (d.defaultValue)));
                break;
            case PKind::choice:
            {
                juce::StringArray sa;
                for (int c = 0; c < d.numChoices; ++c)
                    sa.add (d.choices[c]);
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    pid, d.id, sa, static_cast<int> (d.defaultValue)));
                break;
            }
            case PKind::floating:
            default:
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, d.id, makeRange (d), d.defaultValue));
                break;
        }
    }
    return layout;
}

// Audio-thread parameter access (CLAUDE.md rule 4): raw atomic pointers,
// cached once on the message thread after APVTS construction.
struct RawParams
{
    std::atomic<float>* v[kNumParams] {};

    void attach (juce::AudioProcessorValueTreeState& s)
    {
        for (int i = 0; i < kNumParams; ++i)
            v[i] = s.getRawParameterValue (paramDef (static_cast<PId> (i)).id);
    }

    // Audio thread: 61 relaxed atomic loads + a switch per field. No locks,
    // no allocation, no strings.
    void toSnapshot (ParamSnapshot& p) const noexcept
    {
        for (int i = 0; i < kNumParams; ++i)
            applyToSnapshot (p, static_cast<PId> (i),
                             v[i]->load (std::memory_order_relaxed));
    }
};

// ---- preset JSON -> APVTS (message thread only) ----------------------------

inline void resetParamsToDefaults (juce::AudioProcessorValueTreeState& s)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (auto* p = s.getParameter (paramDef (static_cast<PId> (i)).id))
            p->setValueNotifyingHost (p->getDefaultValue());
    }
}

// Applies the "params" object of a preset var on top of whatever the APVTS
// currently holds (call resetParamsToDefaults first for SPEC-default base).
// Unknown IDs are ignored (forward compatibility).
inline bool applyPresetVarToApvts (const juce::var& presetRoot,
                                   juce::AudioProcessorValueTreeState& s,
                                   juce::String& error)
{
    auto* obj = presetRoot.getDynamicObject();
    if (obj == nullptr) { error = "preset root is not an object"; return false; }

    const auto paramsVar = obj->getProperty ("params");
    if (paramsVar.isVoid())
        return true;
    auto* params = paramsVar.getDynamicObject();
    if (params == nullptr) { error = "\"params\" is not an object"; return false; }

    for (const auto& prop : params->getProperties())
    {
        PId id {};
        if (! findParam (prop.name.toString(), id))
            continue;
        if (auto* p = s.getParameter (paramDef (id).id))
        {
            const float plain = plainFromVar (id, prop.value);
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
        }
    }
    return true;
}

// ---- crafted snapshot -> APVTS (message thread only, Phase 4) --------------

// Writes every field of a (crafted) ParamSnapshot into the APVTS through
// setValueNotifyingHost — the same atomic path host automation uses, so the
// audio thread picks the change up lock-free and smooths it (~25 ms).
inline void applySnapshotToApvts (const ParamSnapshot& snap,
                                  juce::AudioProcessorValueTreeState& s)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<PId> (i);
        if (auto* p = s.getParameter (paramDef (id).id))
            p->setValueNotifyingHost (p->convertTo0to1 (snapshotToPlain (snap, id)));
    }
}

// ---- APVTS -> preset JSON (message thread only) ----------------------------

// Minimal-override params object: only values differing from the baseline,
// in canonical JSON representation (choices as strings, sub_oct as -1/-2).
// The baseline is SPEC defaults for craft-less patches, or the crafted
// snapshot when a craft grid is set (SPEC: craft first, then params on top —
// so overrides must be a diff against the craft result, and a factory recipe
// preset saves near-zero overrides).
inline juce::var presetParamsVarFromApvts (juce::AudioProcessorValueTreeState& s,
                                           const ParamSnapshot* baseline = nullptr)
{
    auto* obj = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<PId> (i);
        const auto& d = paramDef (id);
        if (auto* raw = s.getRawParameterValue (d.id))
        {
            const float plain = raw->load();
            // Compare against the baseline as the APVTS actually stores it
            // (normalize/denormalize round trip), not the literal — an
            // untouched parameter must never be written as an override.
            const float basePlain = baseline != nullptr
                                  ? snapshotToPlain (*baseline, id)
                                  : d.defaultValue;
            const float baseStored = plainFromVar (id, juce::var (static_cast<double> (basePlain)));
            if (std::abs (plain - baseStored)
                    > 1.0e-6f * std::max (1.0f, std::abs (baseStored)))
                obj->setProperty (d.id, varFromPlain (id, plain));
        }
    }
    return juce::var (obj);
}

// Full preset var per SPEC §Preset format. Since Phase 4 the craft object is
// meaningful: pass the crafted snapshot as baseline so "params" stays a
// minimal diff on top of the craft (empty craft written when none is set).
inline juce::var buildPresetVar (juce::AudioProcessorValueTreeState& s,
                                 const juce::String& name,
                                 const juce::String& category,
                                 const juce::String& author,
                                 const juce::var& craft,
                                 const ParamSnapshot* craftBaseline = nullptr)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("formatVersion", 1);
    obj->setProperty ("name", name);
    obj->setProperty ("category", category);
    obj->setProperty ("author", author);
    if (! craft.isVoid())
        obj->setProperty ("craft", craft);
    else
    {
        auto* c = new juce::DynamicObject();
        c->setProperty ("base", juce::String());
        juce::Array<juce::var> cells;
        for (int i = 0; i < 8; ++i)
            cells.add (juce::String());
        c->setProperty ("cells", cells);
        obj->setProperty ("craft", juce::var (c));
    }
    obj->setProperty ("params", presetParamsVarFromApvts (s, craftBaseline));
    return juce::var (obj);
}

} // namespace blockwave
