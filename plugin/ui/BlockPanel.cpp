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

#include "BlockPanel.h"

namespace blockwave::ui
{

BlockPanel::BlockPanel (const juce::String& t) : title (t)
{
    setOpaque (false);
}

ParamCell& BlockPanel::addCell (std::unique_ptr<ParamCell> cell)
{
    auto& ref = *cell;
    addAndMakeVisible (ref);
    cells.push_back (std::move (cell));
    resized();
    return ref;
}

juce::ToggleButton& BlockPanel::addTitleToggle (const juce::String& tooltip)
{
    titleToggle = std::make_unique<juce::ToggleButton> (juce::String());
    titleToggle->setTooltip (tooltip);
    addAndMakeVisible (*titleToggle);
    resized();
    return *titleToggle;
}

void BlockPanel::paint (juce::Graphics& g)
{
    using namespace colours;
    const auto r = getLocalBounds();
    drawBevelBox (g, r, panelFace, panelLight, panelDark, outline);

    // Title bar with a grass pixel bullet.
    g.setColour (titleBar);
    g.fillRect (3, 3, r.getWidth() - 6, kPanelTitleH - 3);
    g.setColour (grass);
    g.fillRect (6, 7, 4, 4);
    drawPixelText (g, title, 14, 6, 1, label);
}

void BlockPanel::resized()
{
    for (size_t i = 0; i < cells.size(); ++i)
        cells[i]->setBounds (kPanelPad + static_cast<int> (i) * ParamCell::cellW,
                             kPanelTitleH + 8, ParamCell::cellW, ParamCell::cellH);
    if (titleToggle != nullptr)
        titleToggle->setBounds (getWidth() - 27, 3, 22, 12);
}

} // namespace blockwave::ui
