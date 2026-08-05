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

// ORIGINAL 16x16 block art for the CRAFT grid — one sprite per material and
// one glyph per base archetype, drawn from scratch as indexed pixel data (no
// asset files, nothing traced or sampled from any game). Sprites are blitted
// at integer scales only (grid cell = 4x, palette tile = 2x, drag image = 3x)
// so pixels stay crisp under the editor's 1x/2x transform.
//
// Rendering is cheap but not free (up to 256 fillRects per block), so
// components blit a cached Image from BlockImageCache instead of redrawing
// the sprite on every repaint. The cache is per-editor — no static state.

#include <map>
#include "PixelTheme.h"
#include "CraftEngine.h"

namespace blockwave::ui
{

// Dominant colour of a material / base — used by the 12x12 mini recipe icons
// in the preset browser, where a full sprite would be mush.
juce::Colour materialKeyColour (Material m) noexcept;
juce::Colour baseKeyColour (CraftBase b) noexcept;

// 3-words-max tooltips in the product's voice (CRAFT_GRID.md: "cold wide
// long"); mirrors the material character column of the spec table.
const char* materialTooltip (Material m) noexcept;
const char* baseTooltip (CraftBase b) noexcept;

// Draws the 16x16 sprite at (x, y) with the given integer pixel scale.
void drawMaterialSprite (juce::Graphics&, Material, int x, int y, int scale);
void drawBaseSprite (juce::Graphics&, CraftBase, int x, int y, int scale);

// Sprite rendered into an ARGB image (transparent where the sprite is).
juce::Image makeMaterialImage (Material, int scale);
juce::Image makeBaseImage (CraftBase, int scale);

// ---- per-cell WEIGHT / MIX dimming (CRAFT bench) ----------------------------
// A placed block is drawn DARKER as its cell weight drops, so "turned down"
// reads at a glance across the whole bench. The dimming is QUANTISED into
// kWeightDimLevels discrete steps instead of following the weight smoothly:
// this is a pixel synth, and a continuous fade would read as a soft gradient
// wash over sprite art that is otherwise all flat colour. Each level blends
// every palette entry of the sprite toward the SPEC night colour BEFORE the
// sprite is rasterised, so the result is still flat indexed pixel art (no
// translucent overlay, no anti-aliased edge, nothing to blur at 125/175 %).
constexpr int kWeightDimLevels = 5;                // 0 = full bright .. 4 = darkest
int weightDimLevel (float weight01) noexcept;
juce::Image makeMaterialImage (Material, int scale, int dimLevel);

// 12x12 mini 3x3 recipe icon (4x4 px per cell): centre = base colour, outer
// cells = material colours, empty cells = sunken dark. Teaches the crafting
// system straight from the preset browser (SPEC §UI). The scaled overload
// draws the same icon at an integer multiple (cells 4*scale px) with pure
// fillRects — no image resampling, always crisp; the Discoveries page uses
// scale 2 so found patterns survive a screenshot.
constexpr int kMiniIconSize = 12;
void drawMiniCraftIcon (juce::Graphics&, const CraftGrid&, int x, int y);
void drawMiniCraftIcon (juce::Graphics&, const CraftGrid&, int x, int y, int scale);
void drawMiniCraftIconEmpty (juce::Graphics&, int x, int y);

// Per-editor image cache (message thread only).
class BlockImageCache
{
public:
    const juce::Image& material (Material m, int scale);
    const juce::Image& material (Material m, int scale, int dimLevel);
    const juce::Image& base (CraftBase b, int scale);

private:
    std::map<int, juce::Image> images;
};

} // namespace blockwave::ui
