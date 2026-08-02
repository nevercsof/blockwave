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

#include "MaterialArt.h"

namespace blockwave::ui
{
namespace
{

// ---------------------------------------------------------------------------
// Indexed sprite data. '.' = transparent, '1'..'5' = palette entry below.
// Every design is original BLOCKWAVE pixel art (see art/README notes in the
// checkpoint): generic material blocks, nothing traced from any game.
// ---------------------------------------------------------------------------

static const char* const kMaterialSprites[14][16] =
{
    {   // ICE
        "1111111111111111",
        "1444444332332221",
        "1455443332322221",
        "1454435323222221",
        "1444333322222221",
        "1443333322222221",
        "1435333222222221",
        "1333332222222221",
        "1333322222222221",
        "1333322222225221",
        "1332322222222221",
        "1322232222252221",
        "1222232225222221",
        "1222223222222221",
        "1222222222222221",
        "1111111111111111",
    },
    {   // LAVA
        "1111111111111111",
        "1234223333223331",
        "1324333322332231",
        "1322433322332231",
        "1333423333223331",
        "1333253334423331",
        "1322342233354231",
        "1322334233332231",
        "1333225333223321",
        "1333423433223321",
        "1245332433342231",
        "1233332243434231",
        "1333223352333321",
        "1333223342333321",
        "1233332233332231",
        "1111111111111111",
    },
    {   // STONE
        "1111111111111111",
        "1444444444444441",
        "1433333333333321",
        "1433523335233321",
        "1433333333333321",
        "1433335233335321",
        "1433333333333321",
        "1435333333333321",
        "1433333352333321",
        "1433333333353321",
        "1433352333333321",
        "1433333333333521",
        "1435333523533321",
        "1433333333333321",
        "1222222222222221",
        "1111111111111111",
    },
    {   // WOOD
        "1111111111111111",
        "1433523433523421",
        "1433523433523421",
        "1433523433523421",
        "1433523433523421",
        "1433523433523421",
        "1433523553523421",
        "1433525225523421",
        "1433525225523421",
        "1433523553523421",
        "1433523433523421",
        "1433523433523421",
        "1433523433523421",
        "1433523433523421",
        "1433523433523421",
        "1111111111111111",
    },
    {   // GLASS
        "1111111111111111",
        "1444444444444541",
        "1422222222225531",
        "1422222222255231",
        "1422222222552231",
        "1422222225522231",
        "1422222255222231",
        "1422222552222231",
        "1422225522222231",
        "1422255222222231",
        "1422542222222231",
        "1425422222222231",
        "1454222222222231",
        "1542222222222231",
        "1333333333333331",
        "1111111111111111",
    },
    {   // GOLD
        "1111111111111111",
        "1444444433333331",
        "1445544333333331",
        "1454443333332331",
        "1444433333553231",
        "1444333333333331",
        "1443333333333321",
        "1433333333333221",
        "1333333333332221",
        "1333353333322221",
        "1333333333225221",
        "1333333332222221",
        "1323333322222221",
        "1332333222222221",
        "1333332222222221",
        "1111111111111111",
    },
    {   // CRYSTAL
        "1111111111111111",
        "1222222222222221",
        "1222222532222221",
        "1222223533222221",
        "1222233533322221",
        "1222333533332221",
        "1223343533333221",
        "1233444533333321",
        "1233445433333321",
        "1223344223333221",
        "1222344242332221",
        "1222233243222221",
        "1222223233222221",
        "1222222232222221",
        "1222222222222221",
        "1111111111111111",
    },
    {   // VOLT
        "1111111111111111",
        "1222223222222321",
        "1222232225423221",
        "1222322244232221",
        "1223222442322221",
        "1232224423222221",
        "1322254444422221",
        "1222444445222231",
        "1222223244222321",
        "1222232442223221",
        "1222324422232221",
        "1223254222322221",
        "1232442223222221",
        "1322222232222221",
        "1222222322222231",
        "1111111111111111",
    },
    {   // SLIME
        "1111111111111111",
        "1333333333333331",
        "1334543333333331",
        "1335433333333331",
        "1343333333344331",
        "1333333333343331",
        "1333333333333321",
        "1333333223333221",
        "1333332222332221",
        "1333333222322221",
        "1333333323222221",
        "1333443332222221",
        "1333433322222221",
        "1333333222222221",
        "1333332222222221",
        "1111111111111111",
    },
    {   // TNT
        "1111111111111111",
        "1333333553333331",
        "1333333553333331",
        "1224442224442221",
        "1244422244422241",
        "1444222444222441",
        "1442224442224441",
        "1422244422244421",
        "1222444222444221",
        "1224442224442221",
        "1244422244422241",
        "1444222444222441",
        "1442224442224441",
        "1422244422244421",
        "1222444222444221",
        "1111111111111111",
    },
    {   // MOSS
        "1111111111111111",
        "1545445454454541",
        "1454544545445451",
        "1445454454544541",
        "1544545445454451",
        "1454454544545441",
        "1344324432432431",
        "1342334233433321",
        "1234323323332431",
        "1332332333233231",
        "1233233323323331",
        "1323332332333231",
        "1333233233323321",
        "1323323332332331",
        "1332333233233321",
        "1111111111111111",
    },
    {   // SAND
        "1111111111111111",
        "1444444444444441",
        "1232334343232331",
        "1434323233434321",
        "1323343432323341",
        "1343532334343231",
        "1233434323233431",
        "1432323343432321",
        "1334343235334341",
        "1323233434323231",
        "1343432323343431",
        "1232334343235331",
        "1434325233434321",
        "1323343432323341",
        "1222222222222221",
        "1111111111111111",
    },
    {   // OBSIDIAN
        "1111111111111111",
        "1332233222322231",
        "1242223322332221",
        "1235222322234221",
        "1223522332253221",
        "1222342233423321",
        "1322334224222331",
        "1322233443322231",
        "1332223242332231",
        "1233223344232221",
        "1223222334233221",
        "1223322235423321",
        "1222332233542321",
        "1322232223322331",
        "1332233222322231",
        "1111111111111111",
    },
    {   // CLOUD
        "...1111111111...",
        "..144444444441..",
        ".11335533333311.",
        "1433333335333331",
        "1433533333333331",
        "1333333333333331",
        "1333333333333321",
        "1333333333333221",
        "1333333333332221",
        "1333333333322221",
        "1333333333222221",
        "1333333332222221",
        "1333333322222221",
        ".11333322222211.",
        "..133322222221..",
        "...1111111111...",
    },
};

// Base archetype glyphs, drawn over a dark plate ('3' = ink, '4' = accent).
static const char* const kBaseGlyphs[8][16] =
{
    {   // LEAD
        "................",
        "................",
        "................",
        "..33..33..33..3.",
        "..33..33..33..3.",
        "..33..33..33..3.",
        "..33..33..33..3.",
        "..33..33..33..3.",
        "33..33..33..33..",
        "33..33..33..33..",
        "................",
        "....4......4....",
        "....44444444....",
        "................",
        "................",
        "................",
    },
    {   // BASS
        "................",
        "................",
        "................",
        "..333333........",
        "..3....3........",
        "..3....3........",
        "..3....3........",
        "..3....3333333..",
        "333....3......3.",
        "................",
        "................",
        "...4........4...",
        "...4444444444...",
        "................",
        "................",
        "................",
    },
    {   // PAD
        "................",
        "................",
        "....4......4....",
        "...444....444...",
        "..33333333333...",
        "..3.........3...",
        "..3.........3...",
        "333.........3333",
        "................",
        "................",
        "..44..44..44..4.",
        "................",
        "..3333333333333.",
        "................",
        "................",
        "................",
    },
    {   // PLUCK
        "................",
        "................",
        "...3............",
        "...33...........",
        "...333..........",
        "...3333.........",
        "...33333........",
        "...333.333......",
        "...333...333....",
        "...333.....3333.",
        "...333.........3",
        "...333..........",
        "..44444444444444",
        "................",
        "................",
        "................",
    },
    {   // KEYS
        "................",
        "................",
        "..333333333333..",
        "..3.3.3.3.3.33..",
        "..3.3.3.3.3.33..",
        "..3.3.3.3.3.33..",
        "..344344.44344..",
        "..344344.44344..",
        "..3..3..3..3.3..",
        "..3..3..3..3.3..",
        "..3..3..3..3.3..",
        "..333333333333..",
        "................",
        "................",
        "................",
        "................",
    },
    {   // CHIP
        "................",
        "...4......4.....",
        "..333333333333..",
        "4.3..........3.4",
        "..3..3....3..3..",
        "4.3..........3.4",
        "..3....44....3..",
        "4.3...4..4...3.4",
        "..3...4..4...3..",
        "4.3....44....3.4",
        "..3..........3..",
        "4.3..3....3..3.4",
        "..333333333333..",
        "...4......4.....",
        "................",
        "................",
    },
    {   // PERC
        "................",
        "................",
        "....4......4....",
        "...3333333333...",
        "..333333333333..",
        "..333333333333..",
        "..3..........3..",
        "..3..........3..",
        "..3..........3..",
        "..33........33..",
        "...3333333333...",
        "................",
        ".4............4.",
        "................",
        "................",
        "................",
    },
    {   // DRONE
        "................",
        "................",
        "................",
        "................",
        ".....333333.....",
        "....3......3....",
        "...3........3...",
        "3333..........33",
        "................",
        "................",
        "..3333333333333.",
        "................",
        "....4......4....",
        "....4......4....",
        "................",
        "................",
    },
};

// 5-entry palette per material (index 1..5 in the sprite data above).
struct Palette { juce::uint32 c[5]; };

static const Palette kMaterialPalettes[14] =
{
    { { 0xff123a52, 0xff3f7fb4, 0xff6fb8e8, 0xff9fd8ff, 0xffe8f6ff } },  // ICE
    { { 0xff2a0e06, 0xff5a2410, 0xff8b3a12, 0xffff7b1c, 0xffffd24a } },  // LAVA
    { { 0xff3a3a40, 0xff5a5a60, 0xff8b8b8b, 0xffa6a6ae, 0xff6e6e76 } },  // STONE
    { { 0xff2b1c10, 0xff4e3320, 0xff7a5230, 0xffa4703f, 0xff63421f } },  // WOOD
    { { 0xff38566a, 0xff6f96ad, 0xff9fc4dc, 0xffd8eefb, 0xffffffff } },  // GLASS
    { { 0xff5a3d05, 0xffb8860b, 0xffffcc33, 0xffffe98a, 0xfffffbe0 } },  // GOLD
    { { 0xff2d1240, 0xff4a2170, 0xff9a5be0, 0xffc99cff, 0xfff0e2ff } },  // CRYSTAL
    { { 0xff101018, 0xff232a3a, 0xff38445e, 0xffffe14a, 0xff7ff0ff } },  // VOLT
    { { 0xff1e3a12, 0xff3f7a2a, 0xff5cab3f, 0xff9fdf7a, 0xffd6ffb0 } },  // SLIME
    { { 0xff14100c, 0xff2e2620, 0xff4a3c2e, 0xffff7b1c, 0xffffe14a } },  // TNT
    { { 0xff22301a, 0xff55555c, 0xff7d7d74, 0xff4f7a34, 0xff395e22 } },  // MOSS
    { { 0xff6b5a34, 0xffb39a63, 0xffe0cb92, 0xfff2e6bd, 0xff8a7442 } },  // SAND
    { { 0xff08060c, 0xff171325, 0xff241d3a, 0xff6a4fa0, 0xffb79bff } },  // OBSIDIAN
    { { 0xff5a6070, 0xff9aa2b4, 0xffc9d0de, 0xffeef2fa, 0xffffffff } },  // CLOUD
};

// Dominant colour per material (mini icons, drag ghosts, toast accents).
static const juce::uint32 kMaterialKey[14] =
{
    0xff9fd8ff,  // ICE
    0xffff7b1c,  // LAVA
    0xff8b8b8b,  // STONE
    0xff7a5230,  // WOOD
    0xffbfe0f5,  // GLASS
    0xffffcc33,  // GOLD
    0xff9a5be0,  // CRYSTAL
    0xffffe14a,  // VOLT
    0xff5cab3f,  // SLIME
    0xffff5a1c,  // TNT
    0xff4f7a34,  // MOSS
    0xffe0cb92,  // SAND
    0xff3b2f5c,  // OBSIDIAN
    0xffeef2fa,  // CLOUD
};

static const juce::uint32 kBaseKey[8] =
{
    0xff9fd8ff,  // LEAD
    0xffff7b1c,  // BASS
    0xffc99cff,  // PAD
    0xff5cab3f,  // PLUCK
    0xffe8e8f0,  // KEYS
    0xffffe14a,  // CHIP
    0xffff6a5a,  // PERC
    0xff6f96ad,  // DRONE
};

// 3 words max, product voice — the character column of CRAFT_GRID.md.
static const char* const kMaterialTips[14] =
{
    "cold wide long",        // ICE
    "hot molten aggressive", // LAVA
    "dry blunt raw",         // STONE
    "warm mellow round",     // WOOD
    "thin bright delicate",  // GLASS
    "expensive wide polished",// GOLD
    "metallic singing sharp",// CRYSTAL
    "electric jittery motion",// VOLT
    "wobbly gluey slow",     // SLIME
    "percussive boom drop",  // TNT
    "lo-fi chill dusty",     // MOSS
    "gritty noisy texture",  // SAND
    "dark heavy deep",       // OBSIDIAN
    "soft airy distant",     // CLOUD
};

static const char* const kBaseTips[8] =
{
    "thin cutting lead",     // LEAD
    "deep anchored bass",    // BASS
    "wide breathing pad",    // PAD
    "short plucked stab",    // PLUCK
    "hollow honest keys",    // KEYS
    "tiny console blip",     // CHIP
    "drums from squares",    // PERC
    "endless cavern hum",    // DRONE
};

void drawSprite (juce::Graphics& g, const char* const* rows,
                 const juce::uint32* palette, int numColours,
                 int x, int y, int scale)
{
    for (int r = 0; r < 16; ++r)
    {
        const char* row = rows[r];
        for (int c = 0; c < 16; ++c)
        {
            const int idx = row[c] - '1';
            if (idx < 0 || idx >= numColours)
                continue;                     // '.' = transparent
            g.setColour (juce::Colour (palette[idx]));
            g.fillRect (x + c * scale, y + r * scale, scale, scale);
        }
    }
}

juce::Image renderSprite (const char* const* rows, const juce::uint32* palette,
                          int numColours, int scale)
{
    juce::Image img (juce::Image::ARGB, 16 * scale, 16 * scale, true);
    juce::Graphics g (img);
    drawSprite (g, rows, palette, numColours, 0, 0, scale);
    return img;
}

int materialIndex (Material m) noexcept
{
    return juce::jlimit (0, 13, static_cast<int> (m) - 1);
}

} // namespace

juce::Colour materialKeyColour (Material m) noexcept
{
    if (m == Material::none)
        return colours::chip;
    return juce::Colour (kMaterialKey[materialIndex (m)]);
}

juce::Colour baseKeyColour (CraftBase b) noexcept
{
    return juce::Colour (kBaseKey[juce::jlimit (0, 7, static_cast<int> (b))]);
}

const char* materialTooltip (Material m) noexcept
{
    return m == Material::none ? "empty craft slot"
                               : kMaterialTips[materialIndex (m)];
}

const char* baseTooltip (CraftBase b) noexcept
{
    return kBaseTips[juce::jlimit (0, 7, static_cast<int> (b))];
}

void drawMaterialSprite (juce::Graphics& g, Material m, int x, int y, int scale)
{
    if (m == Material::none)
        return;
    const int i = materialIndex (m);
    drawSprite (g, kMaterialSprites[i], kMaterialPalettes[i].c, 5, x, y, scale);
}

void drawBaseSprite (juce::Graphics& g, CraftBase b, int x, int y, int scale)
{
    const int i = juce::jlimit (0, 7, static_cast<int> (b));
    const juce::uint32 pal[4] = { 0xff0e0e14, 0xff26262e,
                                  colours::ice.getARGB(), colours::grass.getARGB() };
    drawSprite (g, kBaseGlyphs[i], pal, 4, x, y, scale);
}

juce::Image makeMaterialImage (Material m, int scale)
{
    if (m == Material::none)
        return {};
    const int i = materialIndex (m);
    return renderSprite (kMaterialSprites[i], kMaterialPalettes[i].c, 5, scale);
}

juce::Image makeBaseImage (CraftBase b, int scale)
{
    const int i = juce::jlimit (0, 7, static_cast<int> (b));
    const juce::uint32 pal[4] = { 0xff0e0e14, 0xff26262e,
                                  colours::ice.getARGB(), colours::grass.getARGB() };
    return renderSprite (kBaseGlyphs[i], pal, 4, scale);
}

void drawMiniCraftIconEmpty (juce::Graphics& g, int x, int y)
{
    // "No craft recipe" — a sunken empty well with a dim dash, so it never
    // reads as a 3x3 grid that happens to be blank.
    g.setColour (colours::chip);
    g.fillRect (x, y, kMiniIconSize, kMiniIconSize);
    g.setColour (colours::panelDark);
    g.drawRect (x, y, kMiniIconSize, kMiniIconSize, 1);
    g.fillRect (x + 3, y + 5, 6, 2);
}

void drawMiniCraftIcon (juce::Graphics& g, const CraftGrid& grid, int x, int y)
{
    drawMiniCraftIcon (g, grid, x, y, 1);
}

void drawMiniCraftIcon (juce::Graphics& g, const CraftGrid& grid, int x, int y,
                        int scale)
{
    // 3x3 of (4*scale)^2 blocks; cell order matches CraftEngine's frozen
    // indexing. Integer fillRects only, so any integer scale stays crisp.
    static const int cellForSlot[9] = { 0, 1, 2, 3, -1, 4, 5, 6, 7 };
    const int cell = 4 * scale;
    for (int s = 0; s < 9; ++s)
    {
        const int cx = x + (s % 3) * cell;
        const int cy = y + (s / 3) * cell;
        juce::Colour c;
        if (cellForSlot[s] < 0)
            c = baseKeyColour (grid.base);
        else
        {
            const auto m = grid.cells[cellForSlot[s]];
            c = m == Material::none ? colours::chip : materialKeyColour (m);
        }
        g.setColour (c);
        g.fillRect (cx, cy, cell, cell);
        if (cellForSlot[s] >= 0 && grid.cells[cellForSlot[s]] == Material::none)
        {
            g.setColour (colours::night);         // sunken empty slot
            g.fillRect (cx + scale, cy + scale, 2 * scale, 2 * scale);
        }
    }
    g.setColour (colours::outline);
    g.drawRect (x, y, kMiniIconSize * scale, kMiniIconSize * scale, scale);
}

const juce::Image& BlockImageCache::material (Material m, int scale)
{
    const int key = 0x10000 + static_cast<int> (m) * 256 + scale;
    auto it = images.find (key);
    if (it == images.end())
        it = images.emplace (key, makeMaterialImage (m, scale)).first;
    return it->second;
}

const juce::Image& BlockImageCache::base (CraftBase b, int scale)
{
    const int key = 0x20000 + static_cast<int> (b) * 256 + scale;
    auto it = images.find (key);
    if (it == images.end())
        it = images.emplace (key, makeBaseImage (b, scale)).first;
    return it->second;
}

} // namespace blockwave::ui
