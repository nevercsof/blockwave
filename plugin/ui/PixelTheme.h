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

// Pixel-art visual constants for the BLOCKWAVE UI (docs/SPEC.md §UI).
// 8-px grid discipline: every layout constant here is a multiple of 8 (or 4
// for sub-cell details). All art is drawn procedurally with fillRect on
// integer coordinates so the 1x/2x integer transform stays crisp.

#include <juce_gui_basics/juce_gui_basics.h>

namespace blockwave::ui
{

// Fixed base canvas. 832 x 456 (both multiples of 8); 2x = 1664 x 912 which
// still fits a 1080p display. Documented in CHECKPOINTS/PHASE-3.md.
constexpr int kCanvasW   = 832;
constexpr int kCanvasH   = 456;
constexpr int kTopBarH   = 40;
constexpr int kTabStripH = 24;
constexpr int kContentY  = kTopBarH + kTabStripH;          // 64
constexpr int kContentH  = kCanvasH - kContentY;           // 392

// Parameter cell / panel metrics (8-px grid).
constexpr int kCellW       = 48;
constexpr int kCellH       = 56;
constexpr int kPanelH      = 88;
constexpr int kPanelPad    = 8;
constexpr int kPanelTitleH = 16;

namespace colours
{
    // SPEC palette (original, not sampled from any game).
    inline const juce::Colour night { 0xff1c1c24 };
    inline const juce::Colour stone { 0xff8b8b8b };
    inline const juce::Colour dirt  { 0xff7a5230 };
    inline const juce::Colour grass { 0xff5cab3f };
    inline const juce::Colour ice   { 0xff9fd8ff };
    inline const juce::Colour lava  { 0xffff7b1c };

    // Derived shades (kept close to the palette, night-tinted greys).
    inline const juce::Colour outline    { 0xff0e0e14 };
    inline const juce::Colour panelFace  { 0xff6e6e76 };
    inline const juce::Colour panelLight { 0xffa6a6ae };
    inline const juce::Colour panelDark  { 0xff3e3e46 };
    inline const juce::Colour titleBar   { 0xff32323c };
    inline const juce::Colour chip       { 0xff26262e };   // LCD-style readout
    inline const juce::Colour label      { 0xffe8e8f0 };
    inline const juce::Colour dimText    { 0xff9a9aa4 };
    inline const juce::Colour speckle    { 0xff23232d };
    inline const juce::Colour buttonFace { 0xff8b8b8b };
    inline const juce::Colour buttonText { 0xff14141a };
    inline const juce::Colour ledOff     { 0xff3a2a22 };
}

// Chunky 2-px bevelled block: 1-px outline, face, light top/left, dark
// bottom/right (swapped when sunken). Pure fillRect — no anti-aliasing.
inline void drawBevelBox (juce::Graphics& g, juce::Rectangle<int> r,
                          juce::Colour face, juce::Colour light,
                          juce::Colour dark, juce::Colour outlineCol,
                          bool sunken = false, int bevel = 2)
{
    g.setColour (outlineCol);
    g.fillRect (r);
    auto inner = r.reduced (1);
    g.setColour (face);
    g.fillRect (inner);
    const auto tl = sunken ? dark : light;
    const auto br = sunken ? light : dark;
    g.setColour (tl);
    g.fillRect (inner.getX(), inner.getY(), inner.getWidth(), bevel);
    g.fillRect (inner.getX(), inner.getY(), bevel, inner.getHeight());
    g.setColour (br);
    g.fillRect (inner.getX(), inner.getBottom() - bevel, inner.getWidth(), bevel);
    g.fillRect (inner.getRight() - bevel, inner.getY(), bevel, inner.getHeight());
}

// Small ice corner ticks marking keyboard focus (crisp, no glow).
inline void drawFocusTicks (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (colours::ice);
    const int t = 3;
    g.fillRect (r.getX(), r.getY(), t, 1);
    g.fillRect (r.getX(), r.getY(), 1, t);
    g.fillRect (r.getRight() - t, r.getY(), t, 1);
    g.fillRect (r.getRight() - 1, r.getY(), 1, t);
    g.fillRect (r.getX(), r.getBottom() - 1, t, 1);
    g.fillRect (r.getX(), r.getBottom() - t, 1, t);
    g.fillRect (r.getRight() - t, r.getBottom() - 1, t, 1);
    g.fillRect (r.getRight() - 1, r.getBottom() - t, 1, t);
}

} // namespace blockwave::ui
