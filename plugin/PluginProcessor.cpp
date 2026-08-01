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

BlockwaveAudioProcessor::BlockwaveAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void BlockwaveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // All allocation happens here and only here (real-time rules, CLAUDE.md).
    // Hosts may exceed the declared block size; give ourselves headroom and
    // chunk in processBlock if a larger block still arrives.
    scratch.setSize (2, juce::jmax (samplesPerBlock, 4096), false, true, true);

    // Phase 1: default internal patch = ParamSnapshot defaults (SPEC table).
    // Phase 2 replaces this with the APVTS-driven snapshot.
    engine.setParams (blockwave::ParamSnapshot {});
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

    // Host tempo (offline render safe: getPlayHead may be null).
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                engine.setTempo (*bpm);

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

juce::AudioProcessorEditor* BlockwaveAudioProcessor::createEditor()
{
    return new BlockwaveAudioProcessorEditor (*this);
}

void BlockwaveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Phase 2 will serialise the full APVTS + craft grid here.
    juce::ValueTree state ("BLOCKWAVE_STATE");
    state.setProperty ("formatVersion", 0, nullptr);
    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void BlockwaveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));
    juce::ignoreUnused (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BlockwaveAudioProcessor();
}
