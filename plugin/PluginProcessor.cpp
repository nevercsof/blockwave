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
    // Phase 0 shell: nothing to allocate yet. All future engine buffers are
    // allocated here and only here (real-time rules, CLAUDE.md).
    juce::ignoreUnused (sampleRate, samplesPerBlock);
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
    juce::ignoreUnused (midiMessages);

    // Phase 0 shell: a synth with no engine yet outputs clean silence.
    buffer.clear();
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
