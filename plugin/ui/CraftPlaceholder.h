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

// CRAFT tab placeholder (Phase 3). Original pixel art only: an empty 3x3
// grid, a from-scratch mallet icon and caution stripes. The real crafting
// bench (drag-drop materials, DICE, MUTATE, keyboard strip) lands in Phase 4.

#include "PixelTheme.h"
#include "PixelFont.h"

namespace blockwave::ui
{

class CraftPlaceholder final : public juce::Component
{
public:
    CraftPlaceholder() { setOpaque (false); }

    void paint (juce::Graphics& g) override
    {
        using namespace colours;

        // Empty 3x3 crafting grid, sunken cells.
        const int cellSize = 56, gap = 8;
        const int gridW = cellSize * 3 + gap * 2;
        const int gx = (getWidth() - gridW) / 2;
        const int gy = 64;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                drawBevelBox (g,
                              { gx + c * (cellSize + gap), gy + r * (cellSize + gap),
                                cellSize, cellSize },
                              chip, panelLight, panelDark, outline, true);

        // Original 16x16 mallet icon (stone head, dirt handle), 2x scale,
        // parked in the centre cell.
        static const char* mallet[16] =
        {
            "................",
            "...11111111.....",
            "..1111111111....",
            "..1122111111....",
            "..1122111111....",
            "..1111111111....",
            "...11111111.....",
            "......33........",
            "......33........",
            "......33........",
            "......33........",
            "......33........",
            "......33........",
            ".....3333.......",
            "................",
            "................",
        };
        const int ms = 2;
        const int mx = gx + (cellSize + gap) + (cellSize - 16 * ms) / 2;
        const int my = gy + (cellSize + gap) + (cellSize - 16 * ms) / 2;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const char ch = mallet[y][x];
                if (ch == '.')
                    continue;
                g.setColour (ch == '1' ? stone
                           : ch == '2' ? panelLight : dirt);
                g.fillRect (mx + x * ms, my + y * ms, ms, ms);
            }

        // Caution stripes above the caption (lava / night blocks),
        // centred band rather than a full-width divider.
        const int stripesY = gy + gridW + 16;
        const int bandW = 384;
        const int bandX = (getWidth() - bandW) / 2;
        for (int x = 0; x < bandW; x += 16)
        {
            g.setColour ((x / 16) % 2 == 0 ? lava : titleBar);
            g.fillRect (bandX + x, stripesY, 16, 4);
        }

        drawPixelTextCentred (g, "CRAFT MODE",
                              { 0, stripesY + 16, getWidth(), 10 }, 2, label);
        drawPixelTextCentred (g, "UNDER CONSTRUCTION",
                              { 0, stripesY + 36, getWidth(), 5 }, 1, lava);
        drawPixelTextCentred (g, "THE CRAFTING GRID ARRIVES IN PHASE 4",
                              { 0, stripesY + 48, getWidth(), 5 }, 1, dimText);
    }
};

} // namespace blockwave::ui
