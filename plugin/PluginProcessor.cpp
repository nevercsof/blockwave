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

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "RecipeData.h"

BlockwaveAudioProcessor::BlockwaveAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", blockwave::createParameterLayout())
{
    rawParams.attach (apvts);
    loadFactoryBank();
    loadRecipeBook();
    presetLibrary.rescanUserPresets();      // lists only; never creates the folder
}

void BlockwaveAudioProcessor::loadRecipeBook()
{
    int size = 0;
    if (const char* data = RecipeData::getNamedResource ("recipes_json", size))
    {
        juce::String err;
        recipeBook.loadFromJson (juce::String::fromUTF8 (data, size), err);
        jassert (err.isEmpty());
    }
}

void BlockwaveAudioProcessor::loadFactoryBank()
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource (
                BinaryData::namedResourceList[i], size))
            presetLibrary.addFactoryPresetJson (juce::String::fromUTF8 (data, size));
    }
}

void BlockwaveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // All allocation happens here and only here (real-time rules, CLAUDE.md).
    // Hosts may exceed the declared block size; give ourselves headroom and
    // chunk in processBlock if a larger block still arrives.
    scratch.setSize (2, juce::jmax (samplesPerBlock, 4096), false, true, true);

    blockwave::ParamSnapshot snap;
    rawParams.toSnapshot (snap);
    engine.setParams (snap);
    engine.prepare (sampleRate, samplesPerBlock);

    // engine.prepare() resets every voice, so nothing the UI was holding is
    // sounding any more — forget the held set rather than leaving entries that
    // would later release someone else's voice. The queue itself is NOT
    // cleared: touching the read index here would race the message thread, and
    // any events still in flight are harmless (they are applied on the next
    // block against a clean engine).
    uiHeld[0] = uiHeld[1] = 0;
    jingle.prepare (sampleRate);
}

void BlockwaveAudioProcessor::releaseResources() {}

bool BlockwaveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void BlockwaveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Host automation -> engine: build the snapshot from the APVTS raw
    // atomics (no locks, no allocation) and hand it to the engine, which
    // smooths every audible parameter internally.
    blockwave::ParamSnapshot snap;
    rawParams.toSnapshot (snap);
    engine.setParams (snap);

    // Host tempo (offline render safe: getPlayHead may be null).
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
            {
                engine.setTempo (*bpm);
                lastBpm.store (*bpm, std::memory_order_relaxed);
            }

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int scratchMax  = scratch.getNumSamples();

    buffer.clear();
    if (scratchMax == 0)
        return;

    // UI keyboard inbox: applied at sample offset 0, before this block's host
    // MIDI walk (see the semantics note in PluginProcessor.h).
    drainUiMidi();

    // Sample-accurate MIDI: render up to each event, then apply it.
    // Chunk to the scratch capacity so oversized host blocks still work.
    int pos = 0;
    auto midiIt = midiMessages.begin();

    while (pos < numSamples)
    {
        int segmentEnd = juce::jmin (numSamples, pos + scratchMax);
        while (midiIt != midiMessages.end() && (*midiIt).samplePosition <= pos)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())
                engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                engine.noteOff (msg.getNoteNumber());
            else if (msg.isPitchWheel())    // fixed ±2 st, smoothed in engine
                engine.setPitchBend (2.0f * static_cast<float> (msg.getPitchWheelValue() - 8192)
                                          / 8192.0f);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                engine.allNotesOff();
                uiHeld[0] = uiHeld[1] = 0;       // nothing is held any more
            }
            ++midiIt;
        }
        if (midiIt != midiMessages.end())
            segmentEnd = juce::jmin (segmentEnd, (*midiIt).samplePosition);
        if (segmentEnd <= pos)
            segmentEnd = juce::jmin (numSamples, pos + 1);

        const int n = segmentEnd - pos;
        engine.process (scratch.getWritePointer (0), scratch.getWritePointer (1), n);

        if (numChannels >= 2)
        {
            buffer.copyFrom (0, pos, scratch, 0, 0, n);
            buffer.copyFrom (1, pos, scratch, 1, 0, n);
        }
        else if (numChannels == 1)
        {
            buffer.copyFrom (0, pos, scratch, 0, 0, n);
            buffer.addFrom (0, pos, scratch, 1, 0, n);
            buffer.applyGain (0, pos, n, 0.5f);
        }
        pos = segmentEnd;
    }

    // UI confirmation sound: strictly after the engine's master gain and
    // softclip, so no patch, FX or master setting can touch it.
    mixDiscoveryJingle (buffer);
}

// ---- UI keyboard (message thread producer) ----------------------------------

void BlockwaveAudioProcessor::uiNoteOn (int midiNote, float velocity)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    blockwave::UiMidiEvent e;
    e.type = blockwave::UiMidiEvent::noteOn;
    e.note = static_cast<std::uint8_t> (midiNote);
    e.velocity = static_cast<std::uint8_t> (
        juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f)));
    uiMidi.push (e);                             // full ring: see overflow policy
}

void BlockwaveAudioProcessor::uiNoteOff (int midiNote)
{
    if (midiNote < 0 || midiNote > 127)
        return;
    blockwave::UiMidiEvent e;
    e.type = blockwave::UiMidiEvent::noteOff;
    e.note = static_cast<std::uint8_t> (midiNote);
    uiMidi.push (e);
}

void BlockwaveAudioProcessor::uiAllNotesOff()
{
    blockwave::UiMidiEvent e;
    e.type = blockwave::UiMidiEvent::allNotesOff;
    uiMidi.push (e);
}

// ---- UI keyboard (audio thread consumer) ------------------------------------

void BlockwaveAudioProcessor::releaseUiHeldNotes() noexcept
{
    // Only the notes the UI is holding — never engine.allNotesOff(), which
    // would take the host's notes with it.
    for (int w = 0; w < 2; ++w)
    {
        if (uiHeld[w] == 0)
            continue;
        for (int b = 0; b < 64; ++b)
            if (((uiHeld[w] >> b) & 1u) != 0)
                engine.noteOff (w * 64 + b);
        uiHeld[w] = 0;
    }
}

void BlockwaveAudioProcessor::drainUiMidi() noexcept
{
    blockwave::UiMidiEvent e;
    while (uiMidi.pop (e))
    {
        const int note = e.note;
        const int w = note >> 6, b = note & 63;
        const std::uint64_t bit = std::uint64_t (1) << b;

        switch (e.type)
        {
            case blockwave::UiMidiEvent::noteOn:
                engine.noteOn (note, static_cast<float> (e.velocity) / 127.0f);
                uiHeld[w] |= bit;
                break;

            case blockwave::UiMidiEvent::noteOff:
                // Ignore an off for a note we never started (e.g. its note-on
                // was dropped by an overflow) — it is not ours to release.
                if ((uiHeld[w] & bit) != 0)
                {
                    engine.noteOff (note);
                    uiHeld[w] &= ~bit;
                }
                break;

            case blockwave::UiMidiEvent::allNotesOff:
                releaseUiHeldNotes();
                break;

            case blockwave::UiMidiEvent::none:
            default:
                break;
        }
    }

    // Overflow policy (src/UiMidiQueue.h): events were dropped, so a note-off
    // may have been lost — release everything the UI is holding. This runs in
    // the same drain that applied the surviving events, so an overflowing
    // burst never reaches the output at all: it degrades to silence, never to
    // a stuck note, a crash or an unbounded loop.
    if (uiMidi.consumeOverflow())
        releaseUiHeldNotes();
}

// ---- discovery jingle -------------------------------------------------------

void BlockwaveAudioProcessor::triggerDiscoveryJingle()
{
    jingleTrigger.store (1, std::memory_order_release);
}

void BlockwaveAudioProcessor::mixDiscoveryJingle (juce::AudioBuffer<float>& buffer) noexcept
{
    // Idle cost: one relaxed load and one bool test. Nothing is written to the
    // buffer unless the jingle is actually sounding, so an untriggered render
    // is bit-identical to one built without this function (null test).
    if (jingleTrigger.load (std::memory_order_relaxed) != 0)
    {
        jingleTrigger.store (0, std::memory_order_relaxed);
        jingle.start();
    }
    if (! jingle.isActive())
        return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels < 1)
        return;

    float* l = buffer.getWritePointer (0);
    float* r = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        if (! jingle.isActive())
            break;
        // masterSoftclip is the same ceiling the engine already ends in: it is
        // exactly the identity below -0.3 dBFS and asymptotes to < 1.0, so the
        // sum can never leave 0 dBFS and quiet material is untouched. The
        // jingle sits at -18 dBFS, so it only engages the curve on material
        // already pinned against the ceiling.
        const float j = jingle.tick();
        l[i] = blockwave::masterSoftclip (l[i] + j);
        if (r != nullptr)
            r[i] = blockwave::masterSoftclip (r[i] + j);
    }
}

double BlockwaveAudioProcessor::getTailLengthSeconds() const
{
    // Honest FX tail: delay feedback decay to -60 dB plus the CAVE RT60, from
    // the current parameter values (atomics only — callable from any thread).
    blockwave::ParamSnapshot snap;
    rawParams.toSnapshot (snap);
    return blockwave::FxChain::tailSeconds (snap,
                                            lastBpm.load (std::memory_order_relaxed));
}

juce::AudioProcessorEditor* BlockwaveAudioProcessor::createEditor()
{
    return new BlockwaveAudioProcessorEditor (*this);
}

// ---- presets (message thread only) -----------------------------------------

bool BlockwaveAudioProcessor::loadPresetVar (const juce::var& presetRoot, juce::String& error)
{
    auto* obj = presetRoot.getDynamicObject();
    if (obj == nullptr) { error = "preset root is not an object"; return false; }

    // SPEC order: defaults, then the craft grid (deterministic CraftEngine +
    // recipe override on an exact pattern match), then params overrides on
    // top. Loading a preset never registers a discovery — hunting stays
    // interactive (setCraftGrid is the only discovery entry point).
    const auto craft = obj->getProperty ("craft");
    blockwave::CraftGrid grid;
    const bool gridValid = blockwave::craftGridFromVar (craft, grid);
    juce::String recipeName;

    blockwave::resetParamsToDefaults (apvts);
    if (gridValid)
        blockwave::applySnapshotToApvts (
            blockwave::craftSnapshotWithRecipes (grid, &recipeBook, &recipeName), apvts);
    if (! blockwave::applyPresetVarToApvts (presetRoot, apvts, error))
        return false;

    const juce::ScopedLock sl (metaLock);
    presetName     = obj->getProperty ("name").toString();
    if (presetName.isEmpty()) presetName = "UNTITLED";
    presetCategory = obj->getProperty ("category").toString();
    presetAuthor   = obj->getProperty ("author").toString();
    craftJson = craft.isVoid() ? juce::String() : juce::JSON::toString (craft, true);
    craftValid = gridValid;
    if (gridValid)
        craftGrid = grid;
    activeRecipeName = recipeName;
    return true;
}

bool BlockwaveAudioProcessor::loadPresetAtIndex (int index, juce::String& error)
{
    if (index < 0 || index >= presetLibrary.getNumPresets())
    {
        error = "preset index out of range";
        return false;
    }
    if (! loadPresetVar (presetLibrary.getPreset (index).root, error))
        return false;
    presetLibrary.setCurrentIndex (index);
    return true;
}

bool BlockwaveAudioProcessor::loadNextPreset (juce::String& error)
{
    return loadPresetAtIndex (presetLibrary.getNextIndex(), error);
}

bool BlockwaveAudioProcessor::loadPrevPreset (juce::String& error)
{
    return loadPresetAtIndex (presetLibrary.getPrevIndex(), error);
}

juce::var BlockwaveAudioProcessor::buildCurrentPresetVar (const juce::String& name,
                                                          const juce::String& category,
                                                          const juce::String& author)
{
    juce::var craft;
    blockwave::ParamSnapshot baseline;
    bool haveBaseline = false;
    {
        const juce::ScopedLock sl (metaLock);
        if (craftJson.isNotEmpty())
            craft = juce::JSON::parse (craftJson);
        if (craftValid)
        {
            // SPEC: craft applies before params on load, so the saved params
            // must be the minimal diff against the crafted snapshot.
            baseline = blockwave::craftSnapshotWithRecipes (craftGrid, &recipeBook);
            haveBaseline = true;
        }
    }
    return blockwave::buildPresetVar (apvts, name, category, author, craft,
                                      haveBaseline ? &baseline : nullptr);
}

bool BlockwaveAudioProcessor::saveCurrentAsUserPreset (const juce::String& name,
                                                       const juce::String& category,
                                                       juce::String& error)
{
    const auto preset = buildCurrentPresetVar (name, category, "User");
    if (! presetLibrary.saveUserPreset (preset, error))
        return false;
    const juce::ScopedLock sl (metaLock);
    presetName = name;
    presetCategory = category;
    presetAuthor = "User";
    return true;
}

juce::String BlockwaveAudioProcessor::getPresetName() const
{
    const juce::ScopedLock sl (metaLock);
    return presetName;
}

juce::String BlockwaveAudioProcessor::getPresetCategory() const
{
    const juce::ScopedLock sl (metaLock);
    return presetCategory;
}

juce::var BlockwaveAudioProcessor::getCraftData() const
{
    const juce::ScopedLock sl (metaLock);
    return craftJson.isNotEmpty() ? juce::JSON::parse (craftJson) : juce::var();
}

// ---- CRAFT grid (Phase 4; message thread only) ------------------------------

bool BlockwaveAudioProcessor::getCraftGrid (blockwave::CraftGrid& out) const
{
    const juce::ScopedLock sl (metaLock);
    if (! craftValid)
        return false;
    out = craftGrid;
    return true;
}

void BlockwaveAudioProcessor::applyCraftGrid (const blockwave::CraftGrid& grid,
                                              bool registerDiscovery)
{
    // Recompute the full parameter set on the message thread; the APVTS
    // write path is atomic and the engine smooths audible params (~25 ms).
    juce::String recipeName;
    const auto snap = blockwave::craftSnapshotWithRecipes (grid, &recipeBook, &recipeName);
    blockwave::applySnapshotToApvts (snap, apvts);

    const bool newDiscovery = registerDiscovery && recipeName.isNotEmpty()
                           && discoveries.add (recipeName);

    const juce::ScopedLock sl (metaLock);
    craftValid = true;
    craftGrid = grid;
    craftJson = juce::JSON::toString (blockwave::craftGridToVar (grid), true);
    activeRecipeName = recipeName;
    if (newDiscovery)
        pendingDiscovery = recipeName;
}

void BlockwaveAudioProcessor::setCraftGrid (const blockwave::CraftGrid& grid)
{
    applyCraftGrid (grid, true);
}

// ---- per-cell weight / mix (message thread only) ----------------------------

float BlockwaveAudioProcessor::getCraftCellWeight (int cellIndex) const
{
    const juce::ScopedLock sl (metaLock);
    if (! craftValid)
        return blockwave::kCellWeightDefault;
    return craftGrid.cellWeight (cellIndex);
}

void BlockwaveAudioProcessor::setCraftCellWeight (int cellIndex, float weight01)
{
    if (cellIndex < 0 || cellIndex >= blockwave::kNumCells)
        return;
    blockwave::CraftGrid grid;
    if (! getCraftGrid (grid))
        return;
    const float w = blockwave::clampCellWeight (weight01);
    if (grid.cellWeight (cellIndex) == w)
        return;                              // nothing to re-craft
    grid.setCellWeight (cellIndex, w);
    // Weights can neither create nor destroy a recipe match, so a weight edit
    // never registers a discovery (the blocks were already placed to get one).
    applyCraftGrid (grid, false);
}

void BlockwaveAudioProcessor::setCraftCellWeights (const float* weights01, int count)
{
    if (weights01 == nullptr || count <= 0)
        return;
    blockwave::CraftGrid grid;
    if (! getCraftGrid (grid))
        return;
    bool changed = false;
    for (int i = 0; i < count && i < blockwave::kNumCells; ++i)
    {
        const float w = blockwave::clampCellWeight (weights01[i]);
        if (grid.cellWeight (i) != w)
        {
            grid.setCellWeight (i, w);
            changed = true;
        }
    }
    if (changed)
        applyCraftGrid (grid, false);
}

void BlockwaveAudioProcessor::clearCraftGrid()
{
    const juce::ScopedLock sl (metaLock);
    craftValid = false;
    craftJson.clear();
    activeRecipeName.clear();
}

void BlockwaveAudioProcessor::diceCraft (std::uint64_t seed)
{
    blockwave::CraftGrid grid;
    if (! getCraftGrid (grid))
        return;                              // DICE needs a base on the grid
    blockwave::craftDice (grid, seed);
    setCraftGrid (grid);
}

void BlockwaveAudioProcessor::diceCraft()
{
    diceCraft (static_cast<std::uint64_t> (juce::Random::getSystemRandom().nextInt64()));
}

void BlockwaveAudioProcessor::mutateCraft (std::uint64_t seed)
{
    blockwave::ParamSnapshot snap;
    rawParams.toSnapshot (snap);             // message thread: atomic reads only
    blockwave::craftMutate (snap, seed);
    blockwave::applySnapshotToApvts (snap, apvts);

    const juce::ScopedLock sl (metaLock);
    activeRecipeName.clear();                // sound departed from the recipe
}

void BlockwaveAudioProcessor::mutateCraft()
{
    mutateCraft (static_cast<std::uint64_t> (juce::Random::getSystemRandom().nextInt64()));
}

juce::String BlockwaveAudioProcessor::getActiveRecipeName() const
{
    const juce::ScopedLock sl (metaLock);
    return activeRecipeName;
}

juce::String BlockwaveAudioProcessor::getCraftAutoName() const
{
    const juce::ScopedLock sl (metaLock);
    if (activeRecipeName.isNotEmpty())
        return activeRecipeName;
    if (! craftValid)
        return {};
    char buf[128];
    return juce::String (blockwave::autoName (craftGrid, buf, sizeof (buf)));
}

bool BlockwaveAudioProcessor::consumeRecipeDiscovery (juce::String& outName)
{
    const juce::ScopedLock sl (metaLock);
    if (pendingDiscovery.isEmpty())
        return false;
    outName = pendingDiscovery;
    pendingDiscovery.clear();
    return true;
}

// ---- host session state -----------------------------------------------------

void BlockwaveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("BLOCKWAVE_STATE");
    root.setProperty ("formatVersion", 1, nullptr);
    {
        const juce::ScopedLock sl (metaLock);
        root.setProperty ("presetName",     presetName, nullptr);
        root.setProperty ("presetCategory", presetCategory, nullptr);
        root.setProperty ("presetAuthor",   presetAuthor, nullptr);
        root.setProperty ("craft",          craftJson, nullptr);
    }
    root.appendChild (apvts.copyState(), nullptr);

    juce::MemoryOutputStream stream (destData, false);
    root.writeToStream (stream);
}

void BlockwaveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto root = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));
    if (! root.isValid() || ! root.hasType ("BLOCKWAVE_STATE"))
        return;
    // formatVersion 0 (Phase-1 shell) carried no parameters; anything >= 1
    // is read best-effort so future minor additions stay backward compatible.
    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params.createCopy());

    const juce::ScopedLock sl (metaLock);
    presetName     = root.getProperty ("presetName", "INIT").toString();
    presetCategory = root.getProperty ("presetCategory", juce::String()).toString();
    presetAuthor   = root.getProperty ("presetAuthor", juce::String()).toString();
    craftJson      = root.getProperty ("craft", juce::String()).toString();

    // Session params are authoritative (they include any tweaks made after
    // crafting) — the grid is restored for the UI but NOT re-applied, and no
    // discovery is registered. The recipe name is recomputed for display.
    craftValid = craftJson.isNotEmpty()
              && blockwave::craftGridFromVar (juce::JSON::parse (craftJson), craftGrid);
    if (craftValid)
    {
        const auto* r = recipeBook.match (craftGrid);
        activeRecipeName = r != nullptr ? r->name : juce::String();
    }
    else
    {
        activeRecipeName.clear();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BlockwaveAudioProcessor();
}
