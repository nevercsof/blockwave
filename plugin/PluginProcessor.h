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

class BlockwaveAudioProcessor final : public juce::AudioProcessor
{
public:
    BlockwaveAudioProcessor();
    ~BlockwaveAudioProcessor() override = default;

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
    juce::var getCraftData() const;                  // opaque until Phase 4

private:
    void loadFactoryBank();

    blockwave::BlockwaveEngine engine;
    blockwave::RawParams rawParams;

    // Last tempo seen on the audio thread; getTailLengthSeconds (any thread)
    // uses it for the delay-time part of the honest tail estimate.
    std::atomic<double> lastBpm { 120.0 };

    blockwave::PresetLibrary presetLibrary { blockwave::PresetLibrary::defaultUserFolder() };

    // Non-automatable state (SPEC): preset identity + craft grid contents.
    // Guarded because hosts may call get/setStateInformation off the message
    // thread; never touched by the audio thread.
    mutable juce::CriticalSection metaLock;
    juce::String presetName { "INIT" }, presetCategory, presetAuthor;
    juce::String craftJson;                          // compact JSON, "" = none

    // Stereo scratch the engine renders into; sized in prepareToPlay only
    // (real-time rules: no allocation in processBlock).
    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockwaveAudioProcessor)
};
