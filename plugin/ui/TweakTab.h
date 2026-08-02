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

// TWEAK tab: the full synth. All 61 SPEC parameters reachable and readable,
// grouped into block panels (OSC A/B/SUB/NOISE, VOICE, FILTER, MASTER,
// ENV1/2, LFO1/2, CRUSH/DELAY/CAVE). FX controls are live APVTS parameters
// with frozen IDs; the FX engine itself lands in Phase 5.
// Fixed pixel layout on the 832x392 content area — no dynamic layout.

#include "BlockPanel.h"

namespace blockwave::ui
{

class TweakTab final : public juce::Component
{
public:
    explicit TweakTab (juce::AudioProcessorValueTreeState& state);

private:
    BlockPanel& panel (const juce::String& title, int x, int y, int w,
                       const char* onParamId = nullptr,
                       const juce::String& onTooltip = juce::String());
    ParamCell& cell (BlockPanel&, PId, const juce::String& label,
                     const juce::String& tooltip = juce::String());

    juce::AudioProcessorValueTreeState& state;
    std::vector<std::unique_ptr<BlockPanel>> panels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> titleAtts;

    // Synced LFO rates render as divisions — repaint the rate readout when
    // the sync switch flips (message-thread ParameterAttachment callbacks).
    ParamCell* lfo1RateCell = nullptr;
    ParamCell* lfo2RateCell = nullptr;
    std::unique_ptr<juce::ParameterAttachment> lfo1SyncWatch, lfo2SyncWatch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TweakTab)
};

} // namespace blockwave::ui
