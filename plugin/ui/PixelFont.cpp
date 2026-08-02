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

#include "PixelFont.h"

namespace blockwave::ui
{

// Each glyph: 5 rows x 3 columns, '1' = pixel on. Original designs.
static const char* glyphRows (juce::juce_wchar c) noexcept
{
    switch (c)
    {
        case 'A': return "010" "101" "111" "101" "101";
        case 'B': return "110" "101" "110" "101" "110";
        case 'C': return "011" "100" "100" "100" "011";
        case 'D': return "110" "101" "101" "101" "110";
        case 'E': return "111" "100" "110" "100" "111";
        case 'F': return "111" "100" "110" "100" "100";
        case 'G': return "011" "100" "101" "101" "011";
        case 'H': return "101" "101" "111" "101" "101";
        case 'I': return "111" "010" "010" "010" "111";
        case 'J': return "001" "001" "001" "101" "010";
        case 'K': return "101" "101" "110" "101" "101";
        case 'L': return "100" "100" "100" "100" "111";
        case 'M': return "101" "111" "101" "101" "101";
        case 'N': return "110" "101" "101" "101" "101";
        case 'O': return "010" "101" "101" "101" "010";
        case 'P': return "110" "101" "110" "100" "100";
        case 'Q': return "010" "101" "101" "010" "001";
        case 'R': return "110" "101" "110" "101" "101";
        case 'S': return "011" "100" "010" "001" "110";
        case 'T': return "111" "010" "010" "010" "010";
        case 'U': return "101" "101" "101" "101" "111";
        case 'V': return "101" "101" "101" "101" "010";
        case 'W': return "101" "101" "101" "111" "101";
        case 'X': return "101" "101" "010" "101" "101";
        case 'Y': return "101" "101" "010" "010" "010";
        case 'Z': return "111" "001" "010" "100" "111";
        case '0': return "111" "101" "101" "101" "111";
        case '1': return "010" "110" "010" "010" "111";
        case '2': return "110" "001" "010" "100" "111";
        case '3': return "110" "001" "010" "001" "110";
        case '4': return "101" "101" "111" "001" "001";
        case '5': return "111" "100" "110" "001" "110";
        case '6': return "011" "100" "110" "101" "010";
        case '7': return "111" "001" "010" "010" "010";
        case '8': return "010" "101" "010" "101" "010";
        case '9': return "010" "101" "011" "001" "110";
        case '-': return "000" "000" "111" "000" "000";
        case '+': return "000" "010" "111" "010" "000";
        case '.': return "000" "000" "000" "000" "010";
        case '/': return "001" "001" "010" "100" "100";
        case '%': return "101" "001" "010" "100" "101";
        case ':': return "000" "010" "000" "010" "000";
        case '<': return "001" "010" "100" "010" "001";
        case '>': return "100" "010" "001" "010" "100";
        case '&': return "010" "101" "010" "101" "011";
        case '_': return "000" "000" "000" "000" "111";
        case '!': return "010" "010" "010" "000" "010";
        case '*': return "101" "010" "111" "010" "101";
        case '(': return "001" "010" "010" "010" "001";
        case ')': return "100" "010" "010" "010" "100";
        case '#': return "101" "111" "101" "111" "101";
        case '=': return "000" "111" "000" "111" "000";
        case '?': return "110" "001" "010" "000" "010";
        default:  return nullptr;
    }
}

int drawPixelText (juce::Graphics& g, const juce::String& text,
                   int x, int y, int scale, juce::Colour colour)
{
    g.setColour (colour);
    const auto upper = text.toUpperCase();
    int cx = x;
    for (int i = 0; i < upper.length(); ++i)
    {
        const auto ch = upper[i];
        if (ch != ' ')
        {
            const char* rows = glyphRows (ch);
            if (rows == nullptr)
                rows = glyphRows ('?');
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 3; ++c)
                    if (rows[r * 3 + c] == '1')
                        g.fillRect (cx + c * scale, y + r * scale, scale, scale);
        }
        cx += 4 * scale;
    }
    return cx - x;
}

int pixelTextWidth (const juce::String& text, int scale)
{
    const int n = text.length();
    return n == 0 ? 0 : n * 4 * scale - scale;
}

void drawPixelTextCentred (juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<int> area, int scale,
                           juce::Colour colour)
{
    const int w = pixelTextWidth (text, scale);
    const int h = pixelTextHeight (scale);
    drawPixelText (g, text,
                   area.getX() + (area.getWidth() - w) / 2,
                   area.getY() + (area.getHeight() - h) / 2,
                   scale, colour);
}

} // namespace blockwave::ui
