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

// Chunky bevelled block panel with a bitmap-font title bar. Holds a row of
// ParamCells and an optional on/off toggle in the title bar (OSC A/B, SUB,
// NOISE). Static frame art is cheap (a handful of fillRects), children are
// separate components so only the touched cell repaints.

#include "ParamCells.h"

namespace blockwave::ui
{

class BlockPanel final : public juce::Component
{
public:
    explicit BlockPanel (const juce::String& title);

    ParamCell& addCell (std::unique_ptr<ParamCell> cell);
    juce::ToggleButton& addTitleToggle (const juce::String& tooltip);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    std::vector<std::unique_ptr<ParamCell>> cells;
    std::unique_ptr<juce::ToggleButton> titleToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockPanel)
};

} // namespace blockwave::ui
