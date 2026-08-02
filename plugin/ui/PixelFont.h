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

// ORIGINAL 3x5 bitmap font, drawn from scratch for BLOCKWAVE (no external
// font files, no traced glyphs). Uppercase A-Z, digits, and the punctuation
// the UI needs. Every glyph is 3 px wide, 5 px tall, 1 px spacing; advance
// is 4 px at scale 1. Rendered with fillRect on integer coordinates only —
// crisp at any integer scale.

#include <juce_gui_basics/juce_gui_basics.h>

namespace blockwave::ui
{

// Draws text (converted to uppercase) at (x, y); returns the advance width.
int drawPixelText (juce::Graphics& g, const juce::String& text,
                   int x, int y, int scale, juce::Colour colour);

// Width in px of text at the given scale (0 for empty text).
int pixelTextWidth (const juce::String& text, int scale);

constexpr int pixelTextHeight (int scale) { return 5 * scale; }

// Centred inside area (horizontally and vertically, integer-snapped).
void drawPixelTextCentred (juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<int> area, int scale,
                           juce::Colour colour);

} // namespace blockwave::ui
