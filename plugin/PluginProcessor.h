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

#include <juce_audio_processors/juce_audio_processors.h>
#include "BlockwaveEngine.h"
#include "BlockwaveApvts.h"
#include "PresetLibrary.h"
#include "CraftJson.h"
#include "UiMidiQueue.h"
#include "DiscoveryJingle.h"

class BlockwaveAudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer
{
public:
    BlockwaveAudioProcessor();
    ~BlockwaveAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- parameters --------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    // ---- presets (message thread only) -------------------------------------
    blockwave::PresetLibrary& getPresetLibrary() { return presetLibrary; }

    // Applies a full preset var (SPEC §Preset format): params reset to SPEC
    // defaults, then overrides; craft carried as opaque data until Phase 4.
    bool loadPresetVar (const juce::var& presetRoot, juce::String& error);
    bool loadPresetAtIndex (int index, juce::String& error);
    bool loadNextPreset (juce::String& error);
    bool loadPrevPreset (juce::String& error);

    // Snapshot of the current patch as a preset var / save it as user preset.
    juce::var buildCurrentPresetVar (const juce::String& name,
                                     const juce::String& category,
                                     const juce::String& author);
    bool saveCurrentAsUserPreset (const juce::String& name,
                                  const juce::String& category,
                                  juce::String& error);

    juce::String getPresetName() const;
    juce::String getPresetCategory() const;
    juce::var getCraftData() const;                  // craft grid as preset-JSON var

    // ---- CRAFT grid (Phase 4; message thread only) -------------------------
    // The grid lives here; every change recomputes the full parameter set on
    // the message thread and applies it through the APVTS atomic path (the
    // engine smooths audible parameters over ~25 ms — no clicks).

    // Current grid. Returns false when no grid is set (INIT / craft-less
    // preset); out is untouched then.
    bool getCraftGrid (blockwave::CraftGrid& out) const;

    // Applies a user grid edit: craft + recipe detection + (new) discovery
    // registration. This is the ONLY entry point that registers discoveries.
    void setCraftGrid (const blockwave::CraftGrid& grid);

    // Forgets the grid (parameters stay as they are).
    void clearCraftGrid();

    // ---- per-cell WEIGHT / MIX (message thread only) -----------------------
    // The on-block slider's back end. Weight is 0..1 (1.0 = 100%, the
    // default); the UI and the host display it as a percentage. Semantics are
    // documented in full in src/CraftEngine.h (CELL WEIGHT block) — the two
    // that matter for the UI:
    //   - a weight change NEVER affects recipe detection, the active recipe
    //     name or the auto-name, and never registers a discovery;
    //   - it re-crafts through the same atomic APVTS path as any other grid
    //     edit, so the engine glides it over ~25 ms (click-free mid-note).
    //
    // ---- WHO OWNS A WEIGHT (addendum 3, post-1.0.0) ------------------------
    // THE APVTS PARAMETER craft_mix_1..8 IS THE AUTHORITY. CraftGrid::weights
    // is a MIRROR of it and never a second store. The reason is the report
    // this work came from: a weight has to be a real host parameter for FL's
    // "last tweaked" to see it, and once a host can move a value, anything
    // else that also stores it will drift the moment automation and a preset
    // load disagree.
    //
    // The invariant, maintained by applyCraftGrid() and asserted by the state
    // tests, is:  craftGrid.weights[c] == craft_mix_(c+1), always.
    //
    // There is ONE write path. Whatever states a weight — the knob, the host,
    // DICE, MUTATE, CLEAR, a preset, a session, tools/render — ends up here:
    //
    //   grid-shaped edits (setCraftGrid / DICE / CLEAR / preset load)
    //       -> writeCellWeightsToApvts()   the grid's weights go THROUGH the
    //                                      parameters, so the host sees them
    //       -> readCellWeightsFromApvts()  the mirror is refreshed FROM the
    //                                      parameters, never from the caller
    //       -> craft + 67 engine parameters
    //
    //   host automation / any external parameter move
    //       -> syncCellWeightsFromApvts()  (message-thread poll)
    //       -> same craft path
    //
    // Because the mirror is always re-read from the parameters, the poll is a
    // no-op unless something outside actually moved a lane.

    // Current weight of a cell. Returns 1.0 for an out-of-range index or when
    // no grid is set. Reads the mirror, which equals the parameter.
    float getCraftCellWeight (int cellIndex) const;

    // Sets one cell's weight (clamped to 0..1) and re-applies the craft.
    // No-op without a grid, for an out-of-range index, or when the weight is
    // already that value (so a slider drag that lands back on its start does
    // not re-write 67 parameters).
    //
    // THIS IS THE CALL THE MIX KNOB SHOULD MAKE. It writes through the
    // craft_mix_N parameter, so the host sees a normal parameter edit. When no
    // gesture is open (see below) it wraps the write in its own
    // begin/endChangeGesture pair, which is what makes FL's "last tweaked"
    // latch onto the knob.
    void setCraftCellWeight (int cellIndex, float weight01);

    // Sets every placed cell at once (nullptr-safe count = kNumCells).
    void setCraftCellWeights (const float* weights01, int count);

    // Drag framing for the knob. Call begin on mouseDown and end on mouseUp
    // around a run of setCraftCellWeight() calls: the host then records one
    // gesture for the whole drag instead of one per pixel. Optional — without
    // them each set is its own self-contained gesture.
    void beginCraftCellWeightGesture (int cellIndex);
    void endCraftCellWeightGesture (int cellIndex);

    // The parameter behind a cell, for a direct attachment (nullptr if the
    // index is out of range). Note the parameter is a PERCENT (0..100) while
    // every weight in this API is 0..1.
    juce::RangedAudioParameter* getCraftCellWeightParameter (int cellIndex) const;

    // Picks up weight moves that did NOT come through this API — i.e. host
    // automation — and re-crafts if any lane actually moved. Returns true when
    // it re-crafted. Called by an internal message-thread timer; exposed so
    // tests and tools can drive it deterministically instead of waiting.
    bool syncCellWeightsFromApvts();

    // DICE: random materials into the outer cells, base kept. No-op without
    // a grid. The seeded variants are deterministic (tests/tools).
    void diceCraft();
    void diceCraft (std::uint64_t seed);

    // MUTATE: small random offsets on the CURRENT parameters (on top of the
    // craft). Grid is kept; the active recipe name is cleared because the
    // sound has departed from the recipe patch.
    void mutateCraft();
    void mutateCraft (std::uint64_t seed);

    // Recipe name when the current grid exactly matches a recipe pattern
    // ("" otherwise); auto-name = recipe name, else material adjectives +
    // base noun ("Frozen Golden Lead"), else "" without a grid.
    juce::String getActiveRecipeName() const;
    juce::String getCraftAutoName() const;

    // UI poll (e.g. from a Timer): returns true ONCE per new discovery and
    // fills outName — trigger the toast + jingle on true.
    bool consumeRecipeDiscovery (juce::String& outName);

    const blockwave::RecipeBook& getRecipeBook() const { return recipeBook; }
    blockwave::DiscoveryStore& getDiscoveries() { return discoveries; }

    // ---- UI audition keyboard (message thread -> audio thread) -------------
    // The CRAFT tab's key strip posts note events here; processBlock drains
    // them at sample offset 0, before the host MIDI walk for that block.
    // Lock-free SPSC (src/UiMidiQueue.h) — never juce::MidiKeyboardState or
    // MidiMessageCollector, both of which lock on the audio-thread side.
    //
    // SEMANTICS (UI notes vs host notes):
    //  - UI and host notes share one voice pool; UI events are applied at the
    //    start of the block, so they are always "earlier" than that block's
    //    host events. Ordering is deterministic.
    //  - the processor tracks exactly which notes the UI is holding.
    //    uiAllNotesOff() releases only those, one engine.noteOff() per note —
    //    it never calls engine.allNotesOff(), so host-held notes survive.
    //  - if the host and the UI hold the SAME note number, releasing the UI
    //    copy also releases the host copy: the voice manager is keyed by note
    //    number (as on hardware, where one key is one key). Documented, not
    //    worked around.
    //  - a host All Notes Off / All Sound Off clears the UI-held set too, so
    //    no phantom entries survive it.
    void uiNoteOn (int midiNote, float velocity);
    void uiNoteOff (int midiNote);
    void uiAllNotesOff();                        // editor teardown, panic

    // ---- discovery jingle --------------------------------------------------
    // Message thread; the audio thread's idle cost is one relaxed atomic load.
    // The jingle is mixed in AFTER the engine's master gain and softclip, so
    // it is independent of the patch and of the FX chain (src/DiscoveryJingle.h).
    void triggerDiscoveryJingle();

private:
    void loadFactoryBank();
    void loadRecipeBook();

    // The one place a grid becomes parameters + stored state. setCraftGrid()
    // is the user-edit entry point (registerDiscovery = true); weight edits
    // go through it with registerDiscovery = false.
    //
    // `grid`'s weights are treated as a STATEMENT OF INTENT, not as storage:
    // they are written through to craft_mix_1..8 and then read straight back,
    // so the craft always runs on exactly what the parameters hold.
    void applyCraftGrid (const blockwave::CraftGrid& grid, bool registerDiscovery);

    // Message-thread poll for host-driven weight moves (see the header comment
    // on the WEIGHT / MIX block). ~60 Hz: well inside the engine's ~25 ms
    // parameter smoothing, so an automation ramp arrives as a continuous glide.
    void timerCallback() override;
    static constexpr int kWeightPollHz = 60;

    // Open knob gestures, one flag per cell. Message thread only.
    bool weightGestureOpen[blockwave::kNumCells] = {};

    // Audio thread only.
    void drainUiMidi() noexcept;
    void releaseUiHeldNotes() noexcept;
    void mixDiscoveryJingle (juce::AudioBuffer<float>&) noexcept;

    blockwave::BlockwaveEngine engine;
    blockwave::RawParams rawParams;

    // Last tempo seen on the audio thread; getTailLengthSeconds (any thread)
    // uses it for the delay-time part of the honest tail estimate.
    std::atomic<double> lastBpm { 120.0 };

    blockwave::PresetLibrary presetLibrary { blockwave::PresetLibrary::defaultUserFolder() };

    // Recipe book (embedded JSON, parsed once in the constructor) and the
    // per-user found-recipe set. Message thread only.
    blockwave::RecipeBook recipeBook;
    blockwave::DiscoveryStore discoveries { blockwave::DiscoveryStore::defaultFile() };

    // Non-automatable state (SPEC): preset identity + craft grid contents.
    // craftGrid.weights is the exception — it is the MIRROR of the automatable
    // craft_mix_1..8 parameters, not state in its own right (see the WEIGHT /
    // MIX block above). Guarded because hosts may call get/setStateInformation
    // off the message thread; never touched by the audio thread.
    mutable juce::CriticalSection metaLock;
    juce::String presetName { "INIT" }, presetCategory, presetAuthor;
    juce::String craftJson;                          // compact JSON, "" = none
    bool craftValid = false;                         // craftGrid meaningful?
    blockwave::CraftGrid craftGrid;
    juce::String activeRecipeName;                   // "" = no recipe active
    juce::String pendingDiscovery;                   // consumed by the UI poll

    // Stereo scratch the engine renders into; sized in prepareToPlay only
    // (real-time rules: no allocation in processBlock).
    juce::AudioBuffer<float> scratch;

    // ---- UI keyboard + jingle state ----------------------------------------
    // The queue is a processor member, so an editor closing mid-flight cannot
    // invalidate what the audio thread is draining. No static mutable state
    // anywhere: every instance owns its own queue, held-note set and jingle.
    blockwave::UiMidiQueue uiMidi;
    std::uint64_t uiHeld[2] { 0, 0 };            // 128-bit note set, AUDIO THREAD
    blockwave::DiscoveryJingle jingle;           // AUDIO THREAD (prepare aside)
    std::atomic<int> jingleTrigger { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockwaveAudioProcessor)
};
