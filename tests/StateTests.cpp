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
//   - Phase-4 UI audio path: the lock-free UI keyboard inbox (uiNoteOn /
//     uiNoteOff / uiAllNotesOff), UI-vs-host note semantics, FIFO overflow,
//     and the discovery jingle (trigger, level ceiling, null test)

#include <atomic>
#include <cstdlib>
#include <new>

#include "PluginProcessor.h"
#include "PresetMapping.h"
#include "BinaryData.h"
#include "TestUtil.h"

// ---------------------------------------------------------------------------
// Global allocation guard (same technique as tests/TestMain.cpp) so the
// Phase-4 UI MIDI + jingle path can be proven allocation-free through the real
// processBlock, not just through the pure components.
static std::atomic<bool> gAllocGuardArmed { false };
static std::atomic<long> gGuardedAllocs { 0 };

static void* countedAlloc (std::size_t size)
{
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
// Phase 4: recipe book JSON must exist, parse, and mirror the pure spec
// patterns (src/CraftEngine.h) exactly — the sync rule in RECIPES_FORMAT.md.
static void test_craft_recipe_book (BlockwaveAudioProcessor& proc)
{
    std::printf ("[craft_recipe_book]\n");
    const auto& book = proc.getRecipeBook();
    CHECK_MSG (book.getNumRecipes() >= 8, "recipe book has %d recipes, expected >= 8",
               book.getNumRecipes());

    int n = 0;
    const auto* pats = specRecipePatterns (n);
    for (int i = 0; i < n; ++i)
    {
        const auto* r = book.findByName (pats[i].name);
        CHECK_MSG (r != nullptr, "spec recipe '%s' missing from JSON book", pats[i].name);
        if (r == nullptr)
            continue;
        CHECK_MSG (r->pattern == pats[i].grid,
                   "'%s': JSON pattern differs from specRecipePatterns()", pats[i].name);
        CHECK_MSG (r->params.getDynamicObject() != nullptr
                       && r->params.getDynamicObject()->getProperties().size() > 0,
                   "'%s': recipe has no override params", pats[i].name);
        CHECK_MSG (categoryRank (r->category) < 8, "'%s': bad category '%s'",
                   pats[i].name, r->category.toRawUTF8());
        // The override must actually change the plain craft result.
        const auto plain = craftApply (pats[i].grid);
        auto overridden = plain;
        applyParamsVarToSnapshot (r->params, overridden);
        CHECK_MSG (hashSnapshot (plain) != hashSnapshot (overridden),
                   "'%s': override patch is a no-op", pats[i].name);
    }
    std::printf ("  %d recipes parsed; 8 spec patterns in sync with CraftEngine\n",
                 book.getNumRecipes());
}

// ---------------------------------------------------------------------------
// SPEC preset order: craft first (incl. recipe override), then params on
// top — and the plugin (APVTS path) must agree with tools/render
// (applyPreset) on the result.
static void test_craft_first_then_params (BlockwaveAudioProcessor& proc)
{
    std::printf ("[craft_first_then_params]\n");
    const auto preset = juce::JSON::parse (juce::String (R"JSON(
    {
      "formatVersion": 1, "name": "CRAFTED", "category": "PAD", "author": "tests",
      "craft": { "base": "PAD", "cells": ["ICE", "", "", "", "", "", "", ""] },
      "params": { "filt_cutoff": 777.0 }
    })JSON"));

    juce::String err;
    CHECK_MSG (proc.loadPresetVar (preset, err), "load failed: %s", err.toRawUTF8());

    RawParams raw;
    raw.attach (proc.apvts);
    ParamSnapshot viaApvts;
    raw.toSnapshot (viaApvts);

    // Pure expectation: craft(PAD+ICE), then the single override.
    CraftGrid g;
    CHECK_MSG (craftGridFromVar (preset.getProperty ("craft", juce::var()), g),
               "craft grid failed to parse");
    ParamSnapshot expected = craftApply (g);
    expected.filt_cutoff = 777.0f;
    const int bad = compareSnapshots (viaApvts, expected, 1.0e-4, "craft_first");
    CHECK_MSG (bad == 0, "%d fields differ from craft-then-params expectation", bad);
    CHECK_MSG (viaApvts.uni_count == 7, "ICE on PAD must give uni_count 7 (got %d)",
               viaApvts.uni_count);

    // tools/render path must produce the same snapshot.
    ParamSnapshot viaRender;
    CHECK_MSG (applyPreset (preset, &proc.getRecipeBook(), viaRender, err),
               "applyPreset failed: %s", err.toRawUTF8());
    const int bad2 = compareSnapshots (viaApvts, viaRender, 1.0e-4, "plugin_vs_render");
    CHECK_MSG (bad2 == 0, "%d fields disagree between plugin and render craft paths", bad2);

    // Grid + recipe state after a preset load; a preset load NEVER discovers.
    CraftGrid restored;
    CHECK_MSG (proc.getCraftGrid (restored) && restored == g,
               "craft grid not restored from preset");
    CHECK_MSG (proc.getActiveRecipeName().isEmpty(),
               "PAD+ICE(1) wrongly matched a recipe");
    std::printf ("  craft-then-params verified on both paths\n");
}

// ---------------------------------------------------------------------------
static void test_processor_craft_api()
{
    std::printf ("[processor_craft_api]\n");
    auto tmpDisc = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("blockwave_craft_tests").getChildFile ("Discoveries.json");
    tmpDisc.getParentDirectory().deleteRecursively();

    BlockwaveAudioProcessor proc;
    proc.getDiscoveries().setFile (tmpDisc);         // never touch the real file
    CHECK_MSG (proc.getDiscoveries().getNumFound() == 0, "temp discoveries not empty");

    // INIT state: no grid.
    CraftGrid g;
    CHECK_MSG (! proc.getCraftGrid (g), "fresh processor claims a craft grid");
    CHECK_MSG (proc.getCraftAutoName().isEmpty(), "fresh processor has an auto-name");

    // Plain craft: params land in the APVTS, auto-name from the tables.
    CraftGrid pad;
    pad.base = CraftBase::PAD;
    pad.cells[0] = Material::ICE;
    proc.setCraftGrid (pad);
    CHECK_MSG (proc.apvts.getRawParameterValue ("uni_count")->load() == 7.0f,
               "setCraftGrid did not reach the APVTS (uni_count)");
    CHECK_MSG (proc.getCraftAutoName() == "Frozen Pad", "auto-name '%s'",
               proc.getCraftAutoName().toRawUTF8());
    CHECK_MSG (proc.getActiveRecipeName().isEmpty(), "non-recipe grid claims a recipe");
    juce::String discovered;
    CHECK_MSG (! proc.consumeRecipeDiscovery (discovered), "phantom discovery");

    // Recipe grid: override applies, discovery fires exactly once, persists.
    int n = 0;
    const auto* pats = specRecipePatterns (n);
    proc.setCraftGrid (pats[0].grid);                // PERMAFROST (PAD + 3xICE)
    CHECK_MSG (proc.getActiveRecipeName() == "PERMAFROST", "recipe not detected: '%s'",
               proc.getActiveRecipeName().toRawUTF8());
    CHECK_MSG (proc.getCraftAutoName() == "PERMAFROST", "recipe name must win auto-name");
    CHECK_MSG (proc.consumeRecipeDiscovery (discovered) && discovered == "PERMAFROST",
               "discovery flag missing");
    CHECK_MSG (! proc.consumeRecipeDiscovery (discovered), "discovery flag not one-shot");
    CHECK_MSG (proc.getDiscoveries().isFound ("PERMAFROST"), "discovery not stored");
    CHECK_MSG (tmpDisc.existsAsFile(), "discoveries file not written");

    // Re-crafting the same recipe is NOT a new discovery.
    proc.setCraftGrid (pats[0].grid);
    CHECK_MSG (! proc.consumeRecipeDiscovery (discovered), "re-discovery fired");

    // The store survives a reload (fresh processor, same file).
    {
        BlockwaveAudioProcessor proc2;
        proc2.getDiscoveries().setFile (tmpDisc);
        CHECK_MSG (proc2.getDiscoveries().getNumFound() == 1
                       && proc2.getDiscoveries().isFound ("PERMAFROST"),
                   "discoveries did not persist across instances");
    }

    // DICE: deterministic per seed, base kept, grid applied.
    proc.setCraftGrid (pad);
    proc.diceCraft (4242);
    CraftGrid d1;
    CHECK_MSG (proc.getCraftGrid (d1), "grid lost after dice");
    proc.setCraftGrid (pad);
    proc.diceCraft (4242);
    CraftGrid d2;
    proc.getCraftGrid (d2);
    CHECK_MSG (d1 == d2, "diceCraft(seed) not deterministic");
    CHECK_MSG (d1.base == CraftBase::PAD, "dice changed the base");

    // MUTATE: params move, stay in range, grid kept, recipe cleared.
    proc.setCraftGrid (pats[0].grid);
    RawParams raw;
    raw.attach (proc.apvts);
    ParamSnapshot before;
    raw.toSnapshot (before);
    proc.mutateCraft (99);
    ParamSnapshot after;
    raw.toSnapshot (after);
    CHECK_MSG (hashSnapshot (before) != hashSnapshot (after), "mutate changed nothing");
    ParamSnapshot clamped = after;
    clampSnapshotToSpecRanges (clamped);
    const int outOfRange = compareSnapshots (after, clamped, 1.0e-6, "mutate_range");
    CHECK_MSG (outOfRange == 0, "mutate left %d params out of SPEC range", outOfRange);
    CraftGrid kept;
    CHECK_MSG (proc.getCraftGrid (kept) && kept == pats[0].grid, "mutate lost the grid");
    CHECK_MSG (proc.getActiveRecipeName().isEmpty(), "mutate must clear the active recipe");

    tmpDisc.getParentDirectory().deleteRecursively();
    std::printf ("  grid apply, recipe discovery, dice, mutate all OK\n");
}

// ---------------------------------------------------------------------------
// Craft save/restore: user preset = craft + minimal diff; session state keeps
// the grid without re-applying it over tweaked params.
static void test_craft_save_and_session()
{
    std::printf ("[craft_save_and_session]\n");
    BlockwaveAudioProcessor a;
    a.getDiscoveries().setFile (
        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("blockwave_craft_tests2").getChildFile ("Discoveries.json"));

    CraftGrid g;
    g.base = CraftBase::KEYS;
    g.cells[2] = Material::WOOD;
    a.setCraftGrid (g);
    if (auto* p = a.apvts.getParameter ("filt_cutoff"))
        p->setValueNotifyingHost (p->convertTo0to1 (777.0f));

    // Save: params must be a minimal diff against the CRAFT baseline.
    const auto preset = a.buildCurrentPresetVar ("WOODY", "KEYS", "tests");
    auto* params = preset.getProperty ("params", juce::var()).getDynamicObject();
    CHECK_MSG (params != nullptr && params->getProperties().size() == 1
                   && params->hasProperty ("filt_cutoff"),
               "craft save wrote %d overrides, expected exactly filt_cutoff",
               params != nullptr ? params->getProperties().size() : -1);

    // Reload -> identical patch (craft first, diff on top).
    BlockwaveAudioProcessor b;
    juce::String err;
    CHECK_MSG (b.loadPresetVar (preset, err), "reload failed: %s", err.toRawUTF8());
    RawParams rawA, rawB;
    rawA.attach (a.apvts);
    rawB.attach (b.apvts);
    ParamSnapshot sa, sb;
    rawA.toSnapshot (sa);
    rawB.toSnapshot (sb);
    CHECK_MSG (compareSnapshots (sa, sb, 1.0e-5, "craft_save") == 0,
               "craft save round trip drifted");
    CraftGrid gb;
    CHECK_MSG (b.getCraftGrid (gb) && gb == g, "craft grid lost in preset round trip");

    // Session: params authoritative, grid restored, no re-apply, no discovery.
    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    BlockwaveAudioProcessor c;
    c.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
    ParamSnapshot sc;
    RawParams rawC;
    rawC.attach (c.apvts);
    rawC.toSnapshot (sc);
    CHECK_MSG (compareSnapshots (sa, sc, 0.0, "craft_session") == 0,
               "session round trip changed params (craft re-applied?)");
    CraftGrid gc;
    CHECK_MSG (c.getCraftGrid (gc) && gc == g, "craft grid lost in session state");
    juce::String dummy;
    CHECK_MSG (! c.consumeRecipeDiscovery (dummy), "session restore fired a discovery");

    a.getDiscoveries().getFile().getParentDirectory().deleteRecursively();
    std::printf ("  minimal-diff save, preset + session round trips OK\n");
}

// ---------------------------------------------------------------------------
// Phase 4: the UI audition keyboard path (processor side) and the discovery
// jingle. Everything below drives the REAL processBlock.
// ---------------------------------------------------------------------------

struct ProcRender { std::vector<float> l, r; };

// Renders `blocks` fixed-size blocks. `before(blockIndex, midi)` runs where the
// message thread would: it may fill the host MidiBuffer and/or call the
// uiNoteOn/uiNoteOff/triggerDiscoveryJingle producers.
template <typename Fn>
static ProcRender renderProcessor (BlockwaveAudioProcessor& proc, double sr,
                                   int blockSize, int blocks, Fn&& before)
{
    proc.prepareToPlay (sr, blockSize);          // resets the engine
    juce::AudioBuffer<float> buf (2, blockSize);
    ProcRender out;
    out.l.reserve (static_cast<size_t> (blockSize) * static_cast<size_t> (blocks));
    out.r.reserve (static_cast<size_t> (blockSize) * static_cast<size_t> (blocks));
    for (int i = 0; i < blocks; ++i)
    {
        juce::MidiBuffer midi;
        before (i, midi);
        buf.clear();
        proc.processBlock (buf, midi);
        const float* l = buf.getReadPointer (0);
        const float* r = buf.getReadPointer (1);
        out.l.insert (out.l.end(), l, l + blockSize);
        out.r.insert (out.r.end(), r, r + blockSize);
    }
    return out;
}

static double rmsOver (const std::vector<float>& x, double sr, double t0, double t1)
{
    const size_t a = static_cast<size_t> (t0 * sr);
    const size_t b = std::min (x.size(), static_cast<size_t> (t1 * sr));
    if (b <= a)
        return 0.0;
    double acc = 0.0;
    for (size_t i = a; i < b; ++i)
        acc += static_cast<double> (x[i]) * x[i];
    return std::sqrt (acc / static_cast<double> (b - a));
}

static float peakOf (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float v : x)
        p = std::max (p, std::abs (v));
    return p;
}

// ---------------------------------------------------------------------------
static void test_ui_keyboard_path()
{
    std::printf ("[ui_keyboard_path]\n");
    const double sr = 48000.0;
    const int bs = 512;
    const int blocks = static_cast<int> (2.0 * sr / bs);      // ~2 s
    BlockwaveAudioProcessor proc;

    // 1) A UI note-on makes sound, at the right pitch (C4 = 261.626 Hz).
    const auto held = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer&) { if (i == 0) proc.uiNoteOn (60, 0.8f); });
    const double f0 = measureF0 (held.l, static_cast<int> (0.6 * sr),
                                 static_cast<int> (1.4 * sr), sr);
    const double rmsHeld = rmsOver (held.l, sr, 1.0, 1.9);
    std::printf ("  UI note C4: %.3f Hz, sustain RMS %.5f\n", f0, rmsHeld);
    CHECK_MSG (rmsHeld > 1.0e-3, "UI note produced no sustained sound (RMS %.7f)", rmsHeld);
    CHECK_MSG (std::abs (centsDiff (f0, 261.6256)) <= 1.0,
               "UI note pitch off by %.2f cents", centsDiff (f0, 261.6256));

    // 2) uiAllNotesOff kills a held UI note (editor-teardown path).
    const auto panicked = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer&)
        {
            if (i == 0) proc.uiNoteOn (60, 0.8f);
            if (i == 47) proc.uiAllNotesOff();                // ~0.5 s
        });
    const double rmsAfterPanic = rmsOver (panicked.l, sr, 1.0, 1.9);
    std::printf ("  after uiAllNotesOff: RMS %.8f (held was %.5f)\n",
                 rmsAfterPanic, rmsHeld);
    CHECK_MSG (rmsOver (panicked.l, sr, 0.2, 0.45) > 1.0e-3,
               "UI note was not sounding before the panic");
    CHECK_MSG (rmsAfterPanic < 1.0e-6, "uiAllNotesOff left a stuck UI note (RMS %.8f)",
               rmsAfterPanic);

    // 3) A plain uiNoteOff releases the note too.
    const auto released = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer&)
        {
            if (i == 0) proc.uiNoteOn (60, 0.8f);
            if (i == 47) proc.uiNoteOff (60);
        });
    CHECK_MSG (rmsOver (released.l, sr, 1.0, 1.9) < 1.0e-6,
               "uiNoteOff left the note sounding");

    // 4) UI notes must NOT cancel host notes. Host holds C4 for the whole
    //    render; the UI plays G4 and then panics. The tail must match the
    //    host-only render.
    const auto hostOnly = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer& midi)
        {
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        });
    const auto hostPlusUi = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer& midi)
        {
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (i == 20) proc.uiNoteOn (67, 0.8f);
            if (i == 47) proc.uiAllNotesOff();
        });
    const double rmsHostOnly = rmsOver (hostOnly.l, sr, 1.2, 1.9);
    const double rmsCoexist  = rmsOver (hostPlusUi.l, sr, 1.2, 1.9);
    std::printf ("  host-only tail RMS %.5f vs host+UI-after-panic %.5f\n",
                 rmsHostOnly, rmsCoexist);
    CHECK_MSG (rmsHostOnly > 1.0e-3, "host note produced no sound");
    CHECK_MSG (std::abs (rmsCoexist - rmsHostOnly) < 0.02 * rmsHostOnly,
               "uiAllNotesOff disturbed the host note (%.6f vs %.6f)",
               rmsCoexist, rmsHostOnly);
    CHECK_MSG (rmsOver (hostPlusUi.l, sr, 0.3, 0.45) > rmsHostOnly,
               "the UI note never joined the host note");

    // 5) A host All Notes Off clears the UI-held set: the later uiAllNotesOff
    //    must not resurrect or disturb anything.
    const auto hostPanic = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer& midi)
        {
            if (i == 0) proc.uiNoteOn (60, 0.8f);
            if (i == 20)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            if (i == 40) proc.uiAllNotesOff();
        });
    CHECK_MSG (rmsOver (hostPanic.l, sr, 1.0, 1.9) < 1.0e-6,
               "host all-notes-off did not silence the UI note");

    std::printf ("  UI note on/off, panic, and host coexistence OK\n");
}

// ---------------------------------------------------------------------------
// Buffer-size changes: the FIFO and the held-note set must survive wildly
// varying block sizes, and a host re-prepare must leave no stuck state.
static void test_ui_keyboard_buffer_sizes()
{
    std::printf ("[ui_keyboard_buffer_sizes]\n");
    const double sr = 48000.0;
    BlockwaveAudioProcessor proc;
    proc.prepareToPlay (sr, 4096);

    const int sizes[] = { 16, 512, 4096, 37, 1, 2048, 128, 4096, 3, 1024 };
    juce::AudioBuffer<float> buf (2, 4096);
    std::vector<float> out;
    out.reserve (static_cast<size_t> (2.0 * sr));

    proc.uiNoteOn (60, 0.8f);                     // held for the whole render
    int cursor = 0;
    while (out.size() < static_cast<size_t> (1.6 * sr))
    {
        const int n = sizes[cursor++ % 10];
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        view.clear();
        juce::MidiBuffer midi;
        proc.processBlock (view, midi);
        const float* l = view.getReadPointer (0);
        out.insert (out.end(), l, l + n);
    }

    const double f0 = measureF0 (out, static_cast<int> (0.6 * sr),
                                 static_cast<int> (1.4 * sr), sr);
    int nonFinite = 0;
    for (float v : out)
        if (! std::isfinite (v))
            ++nonFinite;
    std::printf ("  block sizes 1..4096 mixed: %.3f Hz, RMS %.5f, %d non-finite\n",
                 f0, rmsOver (out, sr, 1.0, 1.5), nonFinite);
    CHECK_MSG (nonFinite == 0, "%d non-finite samples across mixed block sizes", nonFinite);
    CHECK_MSG (rmsOver (out, sr, 1.0, 1.5) > 1.0e-3,
               "UI note did not survive the block-size changes");
    CHECK_MSG (std::abs (centsDiff (f0, 261.6256)) <= 1.0,
               "pitch drifted across block sizes: %.2f cents", centsDiff (f0, 261.6256));

    // Host re-prepare mid-note: documented semantics — prepareToPlay resets
    // every voice, so the note stops; what must NOT happen is a stuck note or
    // a poisoned queue afterwards.
    proc.prepareToPlay (sr, 256);
    juce::AudioBuffer<float> small (2, 256);
    std::vector<float> afterPrepare;
    for (int i = 0; i < 100; ++i)                 // ~0.53 s
    {
        small.clear();
        juce::MidiBuffer midi;
        if (i == 10) proc.uiNoteOff (60);         // the note-off for the dead note
        proc.processBlock (small, midi);
        const float* l = small.getReadPointer (0);
        afterPrepare.insert (afterPrepare.end(), l, l + 256);
    }
    CHECK_MSG (peakOf (afterPrepare) == 0.0f,
               "engine still sounding after prepareToPlay (peak %.6f)",
               static_cast<double> (peakOf (afterPrepare)));

    // ...and a fresh UI note still plays.
    std::vector<float> revived;
    proc.uiNoteOn (64, 0.8f);
    for (int i = 0; i < 100; ++i)
    {
        small.clear();
        juce::MidiBuffer midi;
        proc.processBlock (small, midi);
        const float* l = small.getReadPointer (0);
        revived.insert (revived.end(), l, l + 256);
    }
    CHECK_MSG (rmsOver (revived, sr, 0.2, 0.5) > 1.0e-3,
               "UI keyboard dead after a buffer-size change");
    proc.uiAllNotesOff();

    std::printf ("  mixed block sizes and a mid-session re-prepare OK\n");
}

// ---------------------------------------------------------------------------
// Overflow policy (src/UiMidiQueue.h): the ring drops the newest events and
// raises a flag; the audio thread turns that into a release of every UI-held
// note, in the same drain that applied the survivors. The burst therefore
// never reaches the output: silence, never a stuck note, never a crash.
static void test_ui_keyboard_fifo_overflow()
{
    std::printf ("[ui_keyboard_fifo_overflow]\n");
    const double sr = 48000.0;
    const int bs = 512;
    const int blocks = static_cast<int> (2.0 * sr / bs);
    BlockwaveAudioProcessor proc;

    const auto flooded = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer&)
        {
            if (i != 0)
                return;
            // 600 events into a 128-slot ring, all note-ONs: 128 land, the
            // rest are dropped, and the overflow flag releases the lot.
            for (int n = 0; n < 600; ++n)
                proc.uiNoteOn (36 + n % 48, 0.8f);
        });

    int nonFinite = 0;
    for (float v : flooded.l)
        if (! std::isfinite (v))
            ++nonFinite;
    const double rmsTail = rmsOver (flooded.l, sr, 1.0, 1.9);
    std::printf ("  600 events into a 128-slot ring: peak %.4f, tail RMS %.8f\n",
                 static_cast<double> (peakOf (flooded.l)), rmsTail);
    CHECK_MSG (nonFinite == 0, "%d non-finite samples after a FIFO overflow", nonFinite);
    CHECK_MSG (rmsTail < 1.0e-6, "FIFO overflow left notes stuck on (RMS %.8f)", rmsTail);
    CHECK_MSG (peakOf (flooded.l) == 0.0f,
               "overflow burst reached the output (peak %.6f); policy is release-in-"
               "the-same-drain, i.e. silence",
               static_cast<double> (peakOf (flooded.l)));

    // The queue is usable again straight afterwards.
    const auto after = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer&) { if (i == 0) proc.uiNoteOn (60, 0.8f); });
    CHECK_MSG (rmsOver (after.l, sr, 1.0, 1.9) > 1.0e-3,
               "UI keyboard dead after an overflow");
    proc.uiAllNotesOff();

    std::printf ("  overflow degrades to a released burst, queue recovers\n");
}

// ---------------------------------------------------------------------------
static void test_discovery_jingle_in_processor()
{
    std::printf ("[discovery_jingle_in_processor]\n");
    const double sr = 48000.0;
    const int bs = 512;
    const int blocks = static_cast<int> (2.0 * sr / bs);
    const int triggerBlock = 47;                              // ~0.5 s
    const int jingleStart = triggerBlock * bs;
    const int jingleLen = DiscoveryJingle::kNumSteps
                        * static_cast<int> (std::lround (DiscoveryJingle::kStepSeconds * sr));

    BlockwaveAudioProcessor proc;

    const auto plain = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer& midi)
        {
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        });
    const auto withJingle = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer& midi)
        {
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (i == triggerBlock)
                proc.triggerDiscoveryJingle();
        });

    // Null test: bit-identical everywhere the jingle is not sounding. This is
    // what keeps every existing golden render valid — nothing in any render
    // path triggers the jingle.
    int beforeDiffs = 0, afterDiffs = 0;
    double maxDiff = 0.0;
    for (size_t i = 0; i < plain.l.size(); ++i)
    {
        const bool diff = plain.l[i] != withJingle.l[i] || plain.r[i] != withJingle.r[i];
        if (i < static_cast<size_t> (jingleStart))
        {
            if (diff) ++beforeDiffs;
        }
        else if (i >= static_cast<size_t> (jingleStart + jingleLen))
        {
            if (diff) ++afterDiffs;
        }
        else
        {
            maxDiff = std::max (maxDiff,
                                std::abs (static_cast<double> (withJingle.l[i] - plain.l[i])));
        }
    }
    std::printf ("  null test: %d diffs before, %d after; jingle peak %.4f\n",
                 beforeDiffs, afterDiffs, maxDiff);
    CHECK_MSG (beforeDiffs == 0, "%d samples differ BEFORE the jingle (not bit-transparent)",
               beforeDiffs);
    CHECK_MSG (afterDiffs == 0, "%d samples differ AFTER the jingle (it never let go)",
               afterDiffs);
    CHECK_MSG (maxDiff > 0.10 && maxDiff < 0.16,
               "jingle contribution %.4f is not near -18 dBFS", maxDiff);
    CHECK_MSG (peakOf (withJingle.l) <= 1.0f && peakOf (withJingle.r) <= 1.0f,
               "jingle pushed the output past 0 dBFS");

    // Untriggered renders are identical to each other too (no hidden state).
    const auto plain2 = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer& midi)
        {
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        });
    int repeatDiffs = 0;
    for (size_t i = 0; i < plain.l.size(); ++i)
        if (plain.l[i] != plain2.l[i])
            ++repeatDiffs;
    CHECK_MSG (repeatDiffs == 0, "untriggered renders are not reproducible (%d diffs)",
               repeatDiffs);

    // Silent patch: the jingle is the only thing in the buffer, and it decays
    // to bitwise silence.
    const auto solo = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer&) { if (i == triggerBlock) proc.triggerDiscoveryJingle(); });
    int preNonZero = 0, postNonZero = 0;
    for (int i = 0; i < jingleStart; ++i)
        if (solo.l[static_cast<size_t> (i)] != 0.0f)
            ++preNonZero;
    for (size_t i = static_cast<size_t> (jingleStart + jingleLen); i < solo.l.size(); ++i)
        if (solo.l[i] != 0.0f)
            ++postNonZero;
    CHECK_MSG (preNonZero == 0, "%d non-zero samples before the trigger", preNonZero);
    CHECK_MSG (postNonZero == 0, "%d non-zero samples after the jingle decayed", postNonZero);
    CHECK_MSG (rmsOver (solo.l, sr, 0.52, 0.75) > 1.0e-3, "the triggered jingle was silent");
    CHECK_MSG (solo.l[static_cast<size_t> (jingleStart)] == 0.0f,
               "the jingle does not start from zero (click)");

    // Patch-independence + ceiling: a deliberately hot patch (everything on,
    // +6 dB master, 8 notes) must still not exceed 0 dBFS with the jingle on.
    const auto setP = [&] (const char* id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    setP ("oscB_on", 1.0f); setP ("sub_on", 1.0f); setP ("noise_on", 1.0f);
    setP ("master_gain", 6.0f); setP ("uni_count", 4.0f);
    const auto hotNotes = [&] (int i, juce::MidiBuffer& midi)
    {
        if (i == 0)
            for (int n = 0; n < 8; ++n)
                midi.addEvent (juce::MidiMessage::noteOn (1, 48 + n * 2,
                                                          (juce::uint8) 127), 0);
    };
    const auto hotDry = renderProcessor (proc, sr, bs, blocks, hotNotes);
    const auto hot = renderProcessor (proc, sr, bs, blocks,
        [&] (int i, juce::MidiBuffer& midi)
        {
            hotNotes (i, midi);
            if (i == triggerBlock)
                proc.triggerDiscoveryJingle();
        });
    // This patch already sits on the engine's softclip ceiling (tanhf saturates
    // to exactly 1.0f in float32, the same bound tests/TestMain.cpp asserts for
    // masterSoftclip), so the meaningful claim is that the jingle does not push
    // the peak any higher.
    std::printf ("  hot patch peak: dry %.9f, with jingle %.9f\n",
                 static_cast<double> (peakOf (hotDry.l)),
                 static_cast<double> (peakOf (hot.l)));
    CHECK_MSG (peakOf (hot.l) <= 1.0f && peakOf (hot.r) <= 1.0f,
               "hot patch + jingle peaked at %.9f (> 0 dBFS)",
               static_cast<double> (peakOf (hot.l)));
    CHECK_MSG (peakOf (hot.l) <= peakOf (hotDry.l) && peakOf (hot.r) <= peakOf (hotDry.r),
               "the jingle raised the peak of an already-clipping patch (%.9f -> %.9f)",
               static_cast<double> (peakOf (hotDry.l)),
               static_cast<double> (peakOf (hot.l)));
    int hotNonFinite = 0;
    for (float v : hot.l)
        if (! std::isfinite (v))
            ++hotNonFinite;
    CHECK_MSG (hotNonFinite == 0, "%d non-finite samples on the hot patch", hotNonFinite);

    std::printf ("  triggers, decays to silence, <= 0 dBFS, bit-transparent when idle\n");
}

// ---------------------------------------------------------------------------
// The real processBlock, with UI MIDI traffic and the jingle live, must not
// allocate. (The pure components get the same treatment in TestMain.cpp.)
static void test_ui_audio_path_no_allocation()
{
    std::printf ("[ui_audio_path_no_allocation]\n");
    BlockwaveAudioProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;                          // reused: no per-block alloc

    // Warm up once outside the guard: the first processBlock touches lazily
    // initialised JUCE state that has nothing to do with our path.
    proc.processBlock (buf, midi);

    long allocs = 0;
    {
        AllocGuard guard;
        for (int block = 0; block < 200; ++block)
        {
            if (block % 5 == 0) proc.uiNoteOn (48 + block % 18, 0.8f);
            if (block % 7 == 0) proc.uiNoteOff (48 + (block - 5) % 18);
            if (block == 100) proc.uiAllNotesOff();
            if (block == 120) proc.triggerDiscoveryJingle();
            if (block == 150)
                for (int n = 0; n < 600; ++n)       // force the overflow branch
                    proc.uiNoteOn (36 + n % 48, 0.7f);
            buf.clear();
            proc.processBlock (buf, midi);
        }
        allocs = guard.count();
    }
    CHECK_MSG (allocs == 0, "processBlock allocated %ld times with UI MIDI + jingle",
               allocs);
    proc.uiAllNotesOff();
    std::printf ("  0 allocations across 200 processBlock calls\n");
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
        test_craft_recipe_book (proc);
        test_craft_first_then_params (proc);
    }
    test_preset_library_model();
    test_session_state_round_trip();
    test_processor_pitch_bend_and_params();
    test_factory_presets_render_clean();
    test_processor_craft_api();
    test_craft_save_and_session();

    // Phase 4: UI audition keyboard + discovery jingle.
    test_ui_keyboard_path();
    test_ui_keyboard_buffer_sizes();
    test_ui_keyboard_fifo_overflow();
    test_discovery_jingle_in_processor();
    test_ui_audio_path_no_allocation();

    std::printf ("\n%d checks, %d failures\n", state().checks, state().failures);
    return state().failures == 0 ? 0 : 1;
}
