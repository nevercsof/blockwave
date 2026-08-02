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

BlockwaveAudioProcessor::BlockwaveAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", blockwave::createParameterLayout())
{
    rawParams.attach (apvts);
    loadFactoryBank();
    presetLibrary.rescanUserPresets();      // lists only; never creates the folder
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
                engine.allNotesOff();
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

    // Craft first (opaque until Phase 4), then params = SPEC defaults + overrides.
    blockwave::resetParamsToDefaults (apvts);
    if (! blockwave::applyPresetVarToApvts (presetRoot, apvts, error))
        return false;

    const juce::ScopedLock sl (metaLock);
    presetName     = obj->getProperty ("name").toString();
    if (presetName.isEmpty()) presetName = "UNTITLED";
    presetCategory = obj->getProperty ("category").toString();
    presetAuthor   = obj->getProperty ("author").toString();
    const auto craft = obj->getProperty ("craft");
    craftJson = craft.isVoid() ? juce::String() : juce::JSON::toString (craft, true);
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
    {
        const juce::ScopedLock sl (metaLock);
        if (craftJson.isNotEmpty())
            craft = juce::JSON::parse (craftJson);
    }
    return blockwave::buildPresetVar (apvts, name, category, author, craft);
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
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BlockwaveAudioProcessor();
}
