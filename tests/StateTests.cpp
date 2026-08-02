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

// BLOCKWAVE Phase-2 state & preset tests (JUCE-linked; the pure-DSP suite
// lives in TestMain.cpp). Covers:
//   - the FROZEN parameter ID list (61 IDs, SPEC order) and SPEC defaults
//   - APVTS default snapshot == ParamSnapshot{} (engine defaults)
//   - preset JSON -> APVTS -> snapshot agrees with tools/render's
//     PresetMapping path (plugin and renderer produce identical patches)
//   - preset save round trip (minimal overrides, canonical representations)
//   - factory bank (8 temporary dev presets, one per category)
//   - PresetLibrary browser model: ordering, next/prev wrap, lazy user folder
//   - host session state round trip (params + preset meta + craft +
//     formatVersion)
//   - processBlock wiring: APVTS -> engine, MIDI pitch bend ±2 st

#include "PluginProcessor.h"
#include "PresetMapping.h"
#include "BinaryData.h"
#include "TestUtil.h"

using namespace blockwave;
using namespace testutil;

// ---------------------------------------------------------------------------
// Snapshot comparison, field by field (auditable; no memcmp — padding).
static int compareSnapshots (const ParamSnapshot& a, const ParamSnapshot& b,
                             double tol, const char* label, double* maxRel = nullptr)
{
    int bad = 0;
    double worst = 0.0;
    const auto num = [&] (const char* name, double x, double y)
    {
        const double scale = std::max ({ std::abs (x), std::abs (y), 1.0 });
        const double rel = std::abs (x - y) / scale;
        worst = std::max (worst, rel);
        if (rel > tol)
        {
            ++bad;
            std::printf ("    MISMATCH %s.%s: %.9g vs %.9g\n", label, name, x, y);
        }
    };
    const auto exact = [&] (const char* name, int x, int y)
    {
        if (x != y)
        {
            ++bad;
            std::printf ("    MISMATCH %s.%s: %d vs %d\n", label, name, x, y);
        }
    };

    exact ("oscA_on", a.oscA_on, b.oscA_on);   exact ("oscB_on", a.oscB_on, b.oscB_on);
    exact ("sub_on", a.sub_on, b.sub_on);      exact ("noise_on", a.noise_on, b.noise_on);
    exact ("oscA_oct", a.oscA_oct, b.oscA_oct); exact ("oscB_oct", a.oscB_oct, b.oscB_oct);
    exact ("oscA_semi", a.oscA_semi, b.oscA_semi); exact ("oscB_semi", a.oscB_semi, b.oscB_semi);
    num ("oscA_fine", a.oscA_fine, b.oscA_fine); num ("oscB_fine", a.oscB_fine, b.oscB_fine);
    num ("oscA_pw", a.oscA_pw, b.oscA_pw);     num ("oscB_pw", a.oscB_pw, b.oscB_pw);
    num ("oscA_level", a.oscA_level, b.oscA_level); num ("oscB_level", a.oscB_level, b.oscB_level);
    exact ("oscB_sync", a.oscB_sync, b.oscB_sync);
    exact ("sub_oct", a.sub_oct, b.sub_oct);   num ("sub_level", a.sub_level, b.sub_level);
    exact ("noise_mode", (int) a.noise_mode, (int) b.noise_mode);
    num ("noise_level", a.noise_level, b.noise_level);
    exact ("uni_count", a.uni_count, b.uni_count);
    num ("uni_detune", a.uni_detune, b.uni_detune); num ("uni_spread", a.uni_spread, b.uni_spread);
    exact ("voice_mode", (int) a.voice_mode, (int) b.voice_mode);
    exact ("poly_count", a.poly_count, b.poly_count);
    num ("glide_time", a.glide_time, b.glide_time);
    exact ("glide_mode", (int) a.glide_mode, (int) b.glide_mode);
    exact ("filt_type", (int) a.filt_type, (int) b.filt_type);
    num ("filt_cutoff", a.filt_cutoff, b.filt_cutoff);
    num ("filt_res", a.filt_res, b.filt_res);  num ("filt_env", a.filt_env, b.filt_env);
    num ("filt_keytrack", a.filt_keytrack, b.filt_keytrack);
    num ("env1_a", a.env1_a, b.env1_a); num ("env1_d", a.env1_d, b.env1_d);
    num ("env1_s", a.env1_s, b.env1_s); num ("env1_r", a.env1_r, b.env1_r);
    num ("env2_a", a.env2_a, b.env2_a); num ("env2_d", a.env2_d, b.env2_d);
    num ("env2_s", a.env2_s, b.env2_s); num ("env2_r", a.env2_r, b.env2_r);
    num ("env2_pitch", a.env2_pitch, b.env2_pitch);
    num ("lfo1_rate", a.lfo1_rate, b.lfo1_rate);
    exact ("lfo1_sync", a.lfo1_sync, b.lfo1_sync);
    num ("lfo1_pwm", a.lfo1_pwm, b.lfo1_pwm);
    num ("lfo2_rate", a.lfo2_rate, b.lfo2_rate);
    exact ("lfo2_sync", a.lfo2_sync, b.lfo2_sync);
    exact ("lfo2_shape", (int) a.lfo2_shape, (int) b.lfo2_shape);
    num ("lfo2_amt", a.lfo2_amt, b.lfo2_amt);
    exact ("lfo2_dest", (int) a.lfo2_dest, (int) b.lfo2_dest);
    num ("vel_amp", a.vel_amp, b.vel_amp);
    exact ("raw", a.raw, b.raw);
    num ("master_gain", a.master_gain, b.master_gain);
    exact ("crush_bits", a.crush_bits, b.crush_bits);
    exact ("crush_down", a.crush_down, b.crush_down);
    num ("crush_mix", a.crush_mix, b.crush_mix);
    exact ("dly_time", a.dly_time, b.dly_time);
    num ("dly_fb", a.dly_fb, b.dly_fb);
    exact ("dly_pingpong", a.dly_pingpong, b.dly_pingpong);
    num ("dly_mix", a.dly_mix, b.dly_mix);
    num ("cave_size", a.cave_size, b.cave_size);
    num ("cave_damp", a.cave_damp, b.cave_damp);
    num ("cave_mix", a.cave_mix, b.cave_mix);

    if (maxRel != nullptr)
        *maxRel = worst;
    return bad;
}

// ---------------------------------------------------------------------------
// The FROZEN parameter ID list — 61 IDs in SPEC-table order. This array is a
// deliberate, independent copy: if src/ParamSpec.h ever drifts, this fails.
static const char* const kFrozenIds[] =
{
    "oscA_on", "oscB_on", "sub_on", "noise_on",
    "oscA_oct", "oscB_oct", "oscA_semi", "oscB_semi",
    "oscA_fine", "oscB_fine", "oscA_pw", "oscB_pw",
    "oscA_level", "oscB_level", "oscB_sync",
    "sub_oct", "sub_level", "noise_mode", "noise_level",
    "uni_count", "uni_detune", "uni_spread",
    "voice_mode", "poly_count", "glide_time", "glide_mode",
    "filt_type", "filt_cutoff", "filt_res", "filt_env", "filt_keytrack",
    "env1_a", "env1_d", "env1_s", "env1_r",
    "env2_a", "env2_d", "env2_s", "env2_r", "env2_pitch",
    "lfo1_rate", "lfo1_sync", "lfo1_pwm",
    "lfo2_rate", "lfo2_sync", "lfo2_shape", "lfo2_amt", "lfo2_dest",
    "crush_bits", "crush_down", "crush_mix",
    "dly_time", "dly_fb", "dly_pingpong", "dly_mix",
    "cave_size", "cave_damp", "cave_mix",
    "vel_amp", "raw", "master_gain",
};

static void test_frozen_parameter_ids (BlockwaveAudioProcessor& proc)
{
    std::printf ("[frozen_parameter_ids]\n");
    constexpr int expected = static_cast<int> (std::size (kFrozenIds));
    CHECK_MSG (expected == 61, "frozen list itself must have 61 entries (has %d)", expected);
    CHECK_MSG (kNumParams == expected, "ParamSpec has %d params, frozen list %d",
               kNumParams, expected);

    const auto& params = proc.getParameters();
    CHECK_MSG (params.size() == expected, "processor exposes %d params, expected %d",
               params.size(), expected);
    int matched = 0;
    for (int i = 0; i < juce::jmin (params.size(), expected); ++i)
    {
        auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[i]);
        CHECK_MSG (wid != nullptr, "param %d has no ID", i);
        if (wid != nullptr && wid->paramID == kFrozenIds[i])
            ++matched;
        else if (wid != nullptr)
            std::printf ("    ID MISMATCH at %d: layout '%s' vs frozen '%s'\n",
                         i, wid->paramID.toRawUTF8(), kFrozenIds[i]);
    }
    CHECK_MSG (matched == expected, "only %d/%d parameter IDs match the frozen list",
               matched, expected);
    std::printf ("  %d parameter IDs verified against the frozen list\n", matched);
}

// ---------------------------------------------------------------------------
static void test_defaults_match_engine (BlockwaveAudioProcessor& proc)
{
    std::printf ("[defaults_match_engine]\n");
    RawParams raw;
    raw.attach (proc.apvts);
    ParamSnapshot fromApvts;
    raw.toSnapshot (fromApvts);
    const ParamSnapshot engineDefaults;
    // The APVTS stores every float default through its normalize/denormalize
    // round trip (float precision), so raw defaults sit within ~1e-7 relative
    // of the ParamSnapshot literals — deterministic and identical on the
    // render side, hence the tiny tolerance rather than bit equality.
    const int bad = compareSnapshots (fromApvts, engineDefaults, 1.0e-5, "defaults");
    CHECK_MSG (bad == 0, "%d APVTS defaults differ from ParamSnapshot defaults", bad);

    // SPEC range spot checks (frozen tapers/ranges).
    const auto range = [&] (const char* id)
    {
        return proc.apvts.getParameterRange (id);
    };
    CHECK_MSG (range ("filt_cutoff").start == 20.0f && range ("filt_cutoff").end == 20000.0f,
               "filt_cutoff range wrong");
    CHECK_MSG (range ("filt_cutoff").skew < 1.0f, "filt_cutoff must be log-tapered");
    CHECK_MSG (range ("oscA_pw").start == 1.0f && range ("oscA_pw").end == 99.0f,
               "oscA_pw range wrong");
    CHECK_MSG (range ("master_gain").start == -60.0f && range ("master_gain").end == 6.0f,
               "master_gain range wrong");
    CHECK_MSG (range ("glide_time").end == 2.0f && range ("glide_time").skew < 1.0f,
               "glide_time must be 0..2 log");
    CHECK_MSG (range ("env1_a").end == 5.0f && range ("env1_a").skew < 1.0f,
               "env1_a must be 0..5 log");
    CHECK_MSG (range ("dly_fb").end == 0.9f, "dly_fb range wrong");
    CHECK_MSG (range ("env2_pitch").start == -48.0f && range ("env2_pitch").end == 48.0f,
               "env2_pitch range wrong");
    std::printf ("  defaults + range spot checks OK\n");
}

// ---------------------------------------------------------------------------
// Every SPEC parameter off-default, choices as canonical strings, plus two
// deliberate out-of-range values that must clamp identically in both paths.
static const char* kStressPresetJson = R"JSON(
{
  "formatVersion": 1,
  "name": "STRESS TEST",
  "category": "FX",
  "author": "tests",
  "craft": { "base": "PAD", "cells": ["ICE", "", "", "", "", "", "", "CLOUD"] },
  "params": {
    "oscA_on": false, "oscB_on": true, "sub_on": true, "noise_on": true,
    "oscA_oct": -1, "oscB_oct": 1, "oscA_semi": 3, "oscB_semi": -7,
    "oscA_fine": 25.5, "oscB_fine": -40.25, "oscA_pw": 12.5, "oscB_pw": 88.0,
    "oscA_level": 0.33, "oscB_level": 0.9, "oscB_sync": true,
    "sub_oct": -2, "sub_level": 0.44, "noise_mode": "short", "noise_level": 0.66,
    "uni_count": 6, "uni_detune": 42.5, "uni_spread": 0.8,
    "voice_mode": "legato", "poly_count": 12, "glide_time": 0.15, "glide_mode": "always",
    "filt_type": "BP", "filt_cutoff": 1450.5, "filt_res": 0.62,
    "filt_env": -0.4, "filt_keytrack": 0.7,
    "env1_a": 0.02, "env1_d": 0.3, "env1_s": 0.5, "env1_r": 0.7,
    "env2_a": 0.01, "env2_d": 0.12, "env2_s": 0.3, "env2_r": 0.2, "env2_pitch": 19.0,
    "lfo1_rate": 0.5, "lfo1_sync": false, "lfo1_pwm": 0.35,
    "lfo2_rate": 55.0, "lfo2_sync": false, "lfo2_shape": "s&h",
    "lfo2_amt": -0.75, "lfo2_dest": "pw",
    "crush_bits": 8, "crush_down": 16, "crush_mix": 0.5,
    "dly_time": "1/8D", "dly_fb": 0.5, "dly_pingpong": false, "dly_mix": 0.3,
    "cave_size": 0.7, "cave_damp": 0.2, "cave_mix": 0.4,
    "vel_amp": 0.9, "raw": true, "master_gain": -66.5
  }
}
)JSON";

static void test_render_plugin_agreement (BlockwaveAudioProcessor& proc)
{
    std::printf ("[render_plugin_agreement]\n");

    std::vector<juce::var> presets;
    presets.push_back (juce::JSON::parse (juce::String (kStressPresetJson)));
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            presets.push_back (juce::JSON::parse (juce::String::fromUTF8 (data, size)));
    }

    RawParams raw;
    raw.attach (proc.apvts);
    double overallWorst = 0.0;
    for (const auto& preset : presets)
    {
        CHECK_MSG (preset.getDynamicObject() != nullptr, "preset JSON failed to parse");
        if (preset.getDynamicObject() == nullptr)
            continue;
        const auto name = preset.getProperty ("name", "?").toString();

        juce::String err;
        CHECK_MSG (proc.loadPresetVar (preset, err), "plugin load failed: %s",
                   err.toRawUTF8());
        ParamSnapshot viaApvts;
        raw.toSnapshot (viaApvts);

        ParamSnapshot viaMapping;                       // tools/render path
        CHECK_MSG (applyPresetParams (preset, viaMapping, err),
                   "PresetMapping failed: %s", err.toRawUTF8());

        double worst = 0.0;
        const int bad = compareSnapshots (viaApvts, viaMapping, 1.0e-4,
                                          name.toRawUTF8(), &worst);
        overallWorst = std::max (overallWorst, worst);
        CHECK_MSG (bad == 0, "'%s': %d fields disagree between plugin and render",
                   name.toRawUTF8(), bad);
    }
    std::printf ("  %d presets agree; worst relative deviation %.3g\n",
                 static_cast<int> (presets.size()), overallWorst);

    // Clamp checks from the stress preset (must land identically): lfo2_rate
    // 55 -> 40 max, master_gain -66.5 -> -60 min.
    ParamSnapshot s;
    juce::String err;
    applyPresetParams (juce::JSON::parse (juce::String (kStressPresetJson)), s, err);
    CHECK_MSG (std::abs (s.lfo2_rate - 40.0f) < 1.0e-3f, "lfo2_rate clamp: %f",
               static_cast<double> (s.lfo2_rate));
    CHECK_MSG (std::abs (s.master_gain + 60.0f) < 1.0e-3f, "master_gain clamp: %f",
               static_cast<double> (s.master_gain));
}

// ---------------------------------------------------------------------------
static void test_preset_save_round_trip (BlockwaveAudioProcessor& proc)
{
    std::printf ("[preset_save_round_trip]\n");
    juce::String err;
    const auto stress = juce::JSON::parse (juce::String (kStressPresetJson));
    CHECK_MSG (proc.loadPresetVar (stress, err), "load failed: %s", err.toRawUTF8());

    // Save the current patch and re-apply the saved JSON through the render
    // mapping: identical snapshot proves the canonical save representation.
    const auto saved = proc.buildCurrentPresetVar ("ROUNDTRIP", "FX", "tests");
    const auto savedJson = juce::JSON::toString (saved);
    const auto reparsed = juce::JSON::parse (savedJson);

    RawParams raw;
    raw.attach (proc.apvts);
    ParamSnapshot current;
    raw.toSnapshot (current);

    ParamSnapshot fromSaved;
    CHECK_MSG (applyPresetParams (reparsed, fromSaved, err),
               "saved JSON re-apply failed: %s", err.toRawUTF8());
    const int bad = compareSnapshots (current, fromSaved, 1.0e-4, "save_round_trip");
    CHECK_MSG (bad == 0, "%d fields lost in save round trip", bad);

    // Craft must be carried through opaquely.
    const auto craft = reparsed.getProperty ("craft", juce::var());
    CHECK_MSG (craft.getProperty ("base", "").toString() == "PAD",
               "craft.base lost in save round trip");
    CHECK_MSG (static_cast<int> (reparsed.getProperty ("formatVersion", 0)) == 1,
               "saved formatVersion != 1");

    // Minimal overrides: defaults must not be written out.
    BlockwaveAudioProcessor fresh;
    const auto defSaved = fresh.buildCurrentPresetVar ("INIT", "", "tests");
    auto* params = defSaved.getProperty ("params", juce::var()).getDynamicObject();
    CHECK_MSG (params != nullptr && params->getProperties().size() == 0,
               "default patch saved %d overrides (expected 0)",
               params != nullptr ? params->getProperties().size() : -1);
    std::printf ("  save -> reload identical; default patch saves 0 overrides\n");
}

// ---------------------------------------------------------------------------
static void test_factory_bank (BlockwaveAudioProcessor& proc)
{
    std::printf ("[factory_bank]\n");
    auto& lib = proc.getPresetLibrary();
    CHECK_MSG (lib.getNumPresets() >= 8, "factory bank has %d presets, expected 8",
               lib.getNumPresets());

    static const char* categories[] = { "LEAD", "BASS", "PLUCK", "PAD",
                                        "KEYS", "CHIP", "PERC", "FX" };
    for (const char* cat : categories)
    {
        int count = 0;
        for (int i = 0; i < lib.getNumPresets(); ++i)
            if (lib.getPreset (i).isFactory && lib.getPreset (i).category == cat)
                ++count;
        CHECK_MSG (count == 1, "category %s has %d factory presets, expected 1", cat, count);
    }

    // Category-grouped order + every preset loads.
    int prevRank = -1;
    for (int i = 0; i < lib.getNumPresets(); ++i)
    {
        const auto& e = lib.getPreset (i);
        const int rank = categoryRank (e.category);
        CHECK_MSG (rank >= prevRank, "browser order broken at %d (%s)", i,
                   e.category.toRawUTF8());
        prevRank = rank;
        juce::String err;
        CHECK_MSG (proc.loadPresetAtIndex (i, err), "preset %d failed to load: %s",
                   i, err.toRawUTF8());
        CHECK_MSG (proc.getPresetName() == e.name, "loaded name mismatch at %d", i);
        CHECK_MSG (e.author.contains ("TEMPORARY"),
                   "dev preset '%s' not marked temporary", e.name.toRawUTF8());
    }
    std::printf ("  8 dev presets: one per category, ordered, all load\n");
}

// ---------------------------------------------------------------------------
static void test_preset_library_model()
{
    std::printf ("[preset_library_model]\n");
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("blockwave_state_tests_presets");
    tmp.deleteRecursively();

    PresetLibrary lib (tmp);
    CHECK_MSG (! tmp.exists(), "user folder must not be created eagerly");
    lib.rescanUserPresets();
    CHECK_MSG (! tmp.exists(), "rescan must not create the user folder");

    lib.addFactoryPresetJson (R"({"formatVersion":1,"name":"F LEAD","category":"LEAD","author":"f","params":{}})");
    lib.addFactoryPresetJson (R"({"formatVersion":1,"name":"F BASS","category":"BASS","author":"f","params":{}})");
    CHECK_MSG (lib.getNumPresets() == 2, "expected 2 factory presets, got %d",
               lib.getNumPresets());
    CHECK_MSG (lib.getPreset (0).category == "LEAD", "LEAD must sort before BASS");

    // Save creates the folder lazily and lists the preset after the factory
    // entry of the same category.
    juce::String err;
    const auto userPreset = juce::JSON::parse (
        R"({"formatVersion":1,"name":"MY BASS","category":"BASS","author":"User","params":{"oscA_pw":25.0}})");
    CHECK_MSG (lib.saveUserPreset (userPreset, err), "save failed: %s", err.toRawUTF8());
    CHECK_MSG (tmp.isDirectory(), "user folder was not created on save");
    CHECK_MSG (lib.getNumPresets() == 3, "expected 3 presets after save, got %d",
               lib.getNumPresets());
    const int idx = lib.indexOfName ("MY BASS");
    CHECK_MSG (idx == 2, "user BASS preset should sort after factory BASS (idx %d)", idx);
    CHECK_MSG (lib.getCurrentIndex() == idx, "save must select the saved preset");
    CHECK_MSG (! lib.getPreset (idx).isFactory, "user preset flagged as factory");

    // next/prev wrap.
    lib.setCurrentIndex (2);
    CHECK_MSG (lib.getNextIndex() == 0, "next must wrap to 0");
    lib.setCurrentIndex (0);
    CHECK_MSG (lib.getPrevIndex() == 2, "prev must wrap to last");

    // Re-scan picks the file back up from disk.
    PresetLibrary lib2 (tmp);
    lib2.rescanUserPresets();
    CHECK_MSG (lib2.indexOfName ("MY BASS") >= 0, "saved preset not found on rescan");

    // Default user folder path (lazy — never created here).
    const auto def = PresetLibrary::defaultUserFolder().getFullPathName();
    CHECK_MSG (def.contains ("Documents") && def.endsWith (
                   juce::String ("BLOCKWAVE") + juce::File::getSeparatorString() + "Presets"),
               "unexpected user folder: %s", def.toRawUTF8());

    tmp.deleteRecursively();
    std::printf ("  ordering, wrap, lazy folder, save/rescan OK\n");
}

// ---------------------------------------------------------------------------
static void test_session_state_round_trip()
{
    std::printf ("[session_state_round_trip]\n");
    BlockwaveAudioProcessor a;
    juce::String err;
    const auto stress = juce::JSON::parse (juce::String (kStressPresetJson));
    CHECK_MSG (a.loadPresetVar (stress, err), "load failed: %s", err.toRawUTF8());

    // Tweak a parameter after the preset load (host automation would do this).
    if (auto* p = a.apvts.getParameter ("filt_cutoff"))
        p->setValueNotifyingHost (p->convertTo0to1 (777.0f));

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    CHECK_MSG (blob.getSize() > 0, "empty state blob");

    BlockwaveAudioProcessor b;
    b.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

    RawParams rawA, rawB;
    rawA.attach (a.apvts);
    rawB.attach (b.apvts);
    ParamSnapshot sa, sb;
    rawA.toSnapshot (sa);
    rawB.toSnapshot (sb);
    const int bad = compareSnapshots (sa, sb, 0.0, "session");
    CHECK_MSG (bad == 0, "%d fields differ after state round trip", bad);

    CHECK_MSG (b.getPresetName() == "STRESS TEST", "preset name lost: '%s'",
               b.getPresetName().toRawUTF8());
    CHECK_MSG (b.getPresetCategory() == "FX", "preset category lost");
    CHECK_MSG (b.getCraftData().getProperty ("base", "").toString() == "PAD",
               "craft data lost in session state");

    // FX params (stored, engine-inert until Phase 5) must survive too.
    CHECK_MSG (b.apvts.getRawParameterValue ("crush_bits")->load() == 8.0f,
               "crush_bits lost in session state");
    CHECK_MSG (b.apvts.getRawParameterValue ("dly_time")->load() == 5.0f,
               "dly_time (choice '1/8D' = index 5) lost in session state");
    std::printf ("  61 params + preset meta + craft survive the round trip\n");
}

// ---------------------------------------------------------------------------
static void test_processor_pitch_bend_and_params()
{
    std::printf ("[processor_pitch_bend_and_params]\n");
    const double sr = 48000.0;
    BlockwaveAudioProcessor proc;
    proc.prepareToPlay (sr, 512);

    const auto renderProc = [&] (bool bend, std::vector<float>& out)
    {
        proc.prepareToPlay (sr, 512);                    // reset engine state
        juce::AudioBuffer<float> buf (2, 512);
        out.clear();
        const int blocks = static_cast<int> (1.5 * sr / 512.0);
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer midi;
            if (i == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
                if (bend)
                    midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
            }
            proc.processBlock (buf, midi);
            const float* l = buf.getReadPointer (0);
            out.insert (out.end(), l, l + 512);
        }
    };

    std::vector<float> plain, bent;
    renderProc (false, plain);
    renderProc (true, bent);

    const double f0 = measureF0 (plain, static_cast<int> (0.6 * sr),
                                 static_cast<int> (1.4 * sr), sr);
    const double f1 = measureF0 (bent, static_cast<int> (0.6 * sr),
                                 static_cast<int> (1.4 * sr), sr);
    const double ref = 220.0 * std::exp2 (2.0 / 12.0);
    std::printf ("  no bend %.3f Hz, full wheel %.3f Hz (target %.3f)\n", f0, f1, ref);
    CHECK_MSG (std::abs (centsDiff (f0, 220.0)) <= 1.0, "unbent pitch off: %.2f cents",
               centsDiff (f0, 220.0));
    CHECK_MSG (std::abs (centsDiff (f1, ref)) <= 2.0, "wheel bend off: %.2f cents",
               centsDiff (f1, ref));

    // APVTS -> engine wiring: closing the filter must change the output.
    if (auto* p = proc.apvts.getParameter ("filt_cutoff"))
        p->setValueNotifyingHost (p->convertTo0to1 (150.0f));
    std::vector<float> dark;
    renderProc (false, dark);
    double rmsPlain = 0.0, rmsDark = 0.0;
    for (size_t i = static_cast<size_t> (0.6 * sr); i < plain.size(); ++i)
    {
        rmsPlain += static_cast<double> (plain[i]) * plain[i];
        rmsDark  += static_cast<double> (dark[i]) * dark[i];
    }
    std::printf ("  open-filter energy %.4f vs cutoff-150Hz energy %.4f\n", rmsPlain, rmsDark);
    CHECK_MSG (rmsDark < 0.5 * rmsPlain,
               "closing filt_cutoff via APVTS did not darken the output");
    if (auto* p = proc.apvts.getParameter ("filt_cutoff"))
        p->setValueNotifyingHost (p->getDefaultValue());
}

// ---------------------------------------------------------------------------
// Phase 5: every factory preset must render clean with the FX block live —
// no NaN/inf, and the master softclip keeps the peak at or below 0 dBFS.
static void test_factory_presets_render_clean()
{
    std::printf ("[factory_presets_render_clean]\n");
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        if (data == nullptr)
            continue;
        const auto preset = juce::JSON::parse (juce::String::fromUTF8 (data, size));
        const auto name = preset.getProperty ("name", "?").toString();

        ParamSnapshot p;
        juce::String err;
        CHECK_MSG (applyPresetParams (preset, p, err), "'%s': mapping failed: %s",
                   name.toRawUTF8(), err.toRawUTF8());

        auto r = renderNote (p, 57, 48000.0, 2.0, 5.0);
        float peak = 0.0f;
        int badSamples = 0;
        for (size_t k = 0; k < r.l.size(); ++k)
        {
            if (! std::isfinite (r.l[k]) || ! std::isfinite (r.r[k]))
                ++badSamples;
            peak = std::max ({ peak, std::abs (r.l[k]), std::abs (r.r[k]) });
        }
        std::printf ("  %-18s peak %.4f, non-finite %d\n", name.toRawUTF8(),
                     static_cast<double> (peak), badSamples);
        CHECK_MSG (badSamples == 0, "'%s': %d non-finite samples", name.toRawUTF8(), badSamples);
        CHECK_MSG (peak <= 1.0f, "'%s': peak %.4f exceeds 0 dBFS", name.toRawUTF8(),
                   static_cast<double> (peak));
        CHECK_MSG (peak > 1.0e-4f, "'%s': rendered silence", name.toRawUTF8());
    }
}

// ---------------------------------------------------------------------------
// Phase 5: getTailLengthSeconds reports the FX tail honestly.
static void test_tail_length_report (BlockwaveAudioProcessor& proc)
{
    std::printf ("[tail_length_report]\n");
    blockwave::resetParamsToDefaults (proc.apvts);
    const double dryTail = proc.getTailLengthSeconds();
    CHECK_MSG (dryTail == 0.0, "dry patch reports %.3f s tail", dryTail);

    const auto set = [&] (const char* id, float plain)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    set ("dly_mix", 0.5f);                       // 1/4 @ 120 BPM default, fb 0.35
    const double dlyTail = proc.getTailLengthSeconds();
    // 0.5 s per hop, log(1e-3)/log(0.35) ~ 6.58 hops ~ 3.3 s.
    CHECK_MSG (dlyTail > 2.0 && dlyTail < 6.0, "delay tail estimate %.2f s off", dlyTail);

    set ("cave_mix", 0.5f);                      // + RT60(0.5) = 3.3 s
    const double bothTail = proc.getTailLengthSeconds();
    CHECK_MSG (bothTail > dlyTail + 2.0 && bothTail < dlyTail + 5.0,
               "delay+cave tail estimate %.2f s off (delay part %.2f)", bothTail, dlyTail);
    std::printf ("  dry 0 s, delay %.2f s, delay+cave %.2f s\n", dlyTail, bothTail);
    blockwave::resetParamsToDefaults (proc.apvts);
}

// ---------------------------------------------------------------------------
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    {
        BlockwaveAudioProcessor proc;
        test_frozen_parameter_ids (proc);
        test_defaults_match_engine (proc);
        test_render_plugin_agreement (proc);
        test_preset_save_round_trip (proc);
        test_factory_bank (proc);
        test_tail_length_report (proc);
    }
    test_preset_library_model();
    test_session_state_round_trip();
    test_processor_pitch_bend_and_params();
    test_factory_presets_render_clean();

    std::printf ("\n%d checks, %d failures\n", state().checks, state().failures);
    return state().failures == 0 ? 0 : 1;
}
