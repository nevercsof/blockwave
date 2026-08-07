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

#include "CraftTab.h"

namespace blockwave::ui
{
namespace
{

// ---- CRAFT tab layout (8-px grid, fits the fixed 832x392 content area) -----
constexpr int kNameX = 16,  kNameY = 8,   kNameW = 592, kNameH = 40;
constexpr int kCountX = 616, kCountW = 200;
constexpr int kGridX = 16,  kGridY = 56;
constexpr int kInfoX = 240, kInfoY = 56,  kInfoW = 152, kInfoH = kCraftGridW;
constexpr int kPalX  = 408, kPalY  = 56;
constexpr int kBtnY  = 208, kBtnW  = 128, kBtnH = 40;
constexpr int kHintX = 16,  kHintY = 272, kHintW = 376, kHintH = 24;
constexpr int kToastX = 408, kToastY = 252, kToastW = 408, kToastH = 52;
constexpr int kKeysY = 320;

// Original 16x16 sparkle used by the discovery toast (two frames, no easing —
// it just pops between big and small).
static const char* const kStarBig[16] =
{
    "................",
    ".......11.......",
    ".......11.......",
    "......1111......",
    "......1111......",
    "..11..1111..11..",
    "...1111111111...",
    "....11111111....",
    "....11111111....",
    "...1111111111...",
    "..11..1111..11..",
    "......1111......",
    "......1111......",
    ".......11.......",
    ".......11.......",
    "................",
};

static const char* const kStarSmall[16] =
{
    "................",
    "................",
    "................",
    "................",
    ".......11.......",
    "......1111......",
    ".....111111.....",
    "...1111111111...",
    "...1111111111...",
    ".....111111.....",
    "......1111......",
    ".......11.......",
    "................",
    "................",
    "................",
    "................",
};

void drawStar (juce::Graphics& g, bool big, int x, int y, int scale,
               juce::Colour c)
{
    const char* const* rows = big ? kStarBig : kStarSmall;
    g.setColour (c);
    for (int r = 0; r < 16; ++r)
        for (int col = 0; col < 16; ++col)
            if (rows[r][col] == '1')
                g.fillRect (x + col * scale, y + r * scale, scale, scale);
}

// Die faces used by the DICE button animation (1..6 pips on a 3x3 layout).
bool diePip (int face, int slot)
{
    // 3x3 pip layout packed 4 bits per row: bit (row * 4 + col).
    static const juce::uint16 masks[6] =
        { 0x020, 0x401, 0x421, 0x505, 0x525, 0x555 };
    const int row = slot / 3, col = slot % 3;
    return ((masks[face - 1] >> (row * 4 + col)) & 1) != 0;
}

} // namespace

// ---- PixelIconButton ---------------------------------------------------------

PixelIconButton::PixelIconButton (const juce::String& t, Glyph gl, int ts)
    : juce::Button (t.isEmpty() ? "icon" : t), text (t), glyph (gl), textScale (ts)
{
    setWantsKeyboardFocus (true);
}

void PixelIconButton::setText (const juce::String& t)
{
    text = t;
    repaint();
}

void PixelIconButton::setSubText (const juce::String& t)
{
    subText = t;
    repaint();
}

void PixelIconButton::startAnimation (int frames)
{
    frame = frames;
    repaint();
}

bool PixelIconButton::animationTick()
{
    if (frame <= 0)
        return false;
    --frame;
    repaint();
    return frame > 0;
}

void PixelIconButton::drawGlyph (juce::Graphics& g, int x, int y, int s) const
{
    using namespace colours;
    switch (glyph)
    {
        case Glyph::dice:
        {
            // Bevelled die; the pip face steps through 4 frames while rolling.
            static const int faceForFrame[4] = { 3, 6, 2, 5 };
            const int face = frame > 0 ? faceForFrame[(frame - 1) % 4] : 5;
            g.setColour (outline);
            g.fillRect (x, y, 16 * s, 16 * s);
            g.setColour (juce::Colour (0xffe8e8f0));
            g.fillRect (x + s, y + s, 14 * s, 14 * s);
            g.setColour (juce::Colour (0xff9a9aa4));
            g.fillRect (x + s, y + 13 * s, 14 * s, 2 * s);
            g.setColour (buttonText);
            for (int slot = 0; slot < 9; ++slot)
                if (diePip (face, slot))
                    g.fillRect (x + s * (3 + (slot % 3) * 4),
                                y + s * (3 + (slot / 3) * 4), 2 * s, 2 * s);
            break;
        }
        case Glyph::mutate:
        {
            // Burst that grows across the 4 animation frames, then rests.
            const int step = frame > 0 ? (4 - ((frame - 1) % 4)) : 2;
            const int cx = x + 8 * s, cy = y + 8 * s;
            g.setColour (lava);
            g.fillRect (cx - s, cy - step * s, 2 * s, step * 2 * s);
            g.fillRect (cx - step * s, cy - s, step * 2 * s, 2 * s);
            g.setColour (ice);
            const int d = juce::jmax (1, step - 1);
            g.fillRect (cx - d * s, cy - d * s, s, s);
            g.fillRect (cx + (d - 1) * s, cy - d * s, s, s);
            g.fillRect (cx - d * s, cy + (d - 1) * s, s, s);
            g.fillRect (cx + (d - 1) * s, cy + (d - 1) * s, s, s);
            break;
        }
        case Glyph::star:
            drawStar (g, frame % 2 == 0, x, y, s, juce::Colour (0xffffcc33));
            break;
        case Glyph::arrowLeft:
        case Glyph::arrowRight:
        {
            g.setColour (buttonText);
            for (int i = 0; i < 6; ++i)             // stepped solid triangle
            {
                const int half = i + 1;
                const int px = glyph == Glyph::arrowLeft ? x + (4 + i) * s
                                                         : x + (9 - i) * s;
                g.fillRect (px, y + (8 - half) * s, s, half * 2 * s);
            }
            break;
        }
        case Glyph::broom:
        {
            g.setColour (dirt);
            g.fillRect (x + 7 * s, y + 2 * s, 2 * s, 7 * s);
            g.setColour (buttonText);
            g.fillRect (x + 4 * s, y + 9 * s, 8 * s, 2 * s);
            for (int i = 0; i < 4; ++i)
                g.fillRect (x + (4 + i * 2) * s, y + 11 * s, s, 3 * s);
            break;
        }
        case Glyph::none:
        default:
            break;
    }
}

void PixelIconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace colours;
    const auto r = getLocalBounds();

    auto face = buttonFace;
    if (down)             face = juce::Colour (0xff6a6a72);
    else if (highlighted) face = juce::Colour (0xff9c9ca4);
    else if (frame > 0)   face = dirt;                  // lit while animating
    drawBevelBox (g, r, face, panelLight, panelDark, outline, down);

    const int gs = r.getHeight() >= 40 ? 2 : 1;
    const int gy = r.getY() + (r.getHeight() - 16 * gs) / 2 + (down ? 1 : 0);

    if (text.isEmpty() && subText.isEmpty())
    {
        drawGlyph (g, r.getX() + (r.getWidth() - 16 * gs) / 2, gy, gs);
    }
    else
    {
        const int gx = r.getX() + 8;
        if (glyph != Glyph::none)
            drawGlyph (g, gx, gy, gs);
        const int tx = gx + (glyph == Glyph::none ? 0 : 16 * gs + 8);
        const int dy = down ? 1 : 0;
        juce::Rectangle<int> textArea (tx, r.getY() + dy,
                                       r.getRight() - 6 - tx, r.getHeight());
        if (subText.isNotEmpty())
        {
            drawPixelText (g, subText, tx, r.getY() + 9 + dy, 1, chip);
            textArea = { tx, r.getY() + 18 + dy, textArea.getWidth(),
                         r.getHeight() - 18 };
        }
        drawPixelTextCentred (g, text, textArea, textScale, buttonText);
    }

    if (hasKeyboardFocus (false))
        drawFocusTicks (g, r);
}

// ---- DiscoveryToast ----------------------------------------------------------

DiscoveryToast::DiscoveryToast()
{
    setInterceptsMouseClicks (false, false);
    setVisible (false);
}

void DiscoveryToast::show (const juce::String& recipeName)
{
    name = recipeName.toUpperCase();
    phase = Phase::entering;
    frame = 3;
    holdTicks = 30;                                  // ~2 s at 15 Hz
    sparkle = 0;
    setVisible (true);
    repaint();
}

void DiscoveryToast::hideNow()
{
    phase = Phase::hidden;
    setVisible (false);
}

bool DiscoveryToast::animationTick()
{
    if (phase == Phase::hidden)
        return false;

    ++sparkle;
    switch (phase)
    {
        case Phase::entering:
            if (--frame <= 0)
                phase = Phase::holding;
            break;
        case Phase::holding:
            if (--holdTicks <= 0)
            {
                phase = Phase::leaving;
                frame = 0;
            }
            break;
        case Phase::leaving:
            if (++frame >= 3)
            {
                hideNow();
                return false;
            }
            break;
        case Phase::hidden:
        default:
            break;
    }
    repaint();
    return true;
}

void DiscoveryToast::paint (juce::Graphics& g)
{
    using namespace colours;
    if (phase == Phase::hidden)
        return;

    // 3 discrete slide steps (12 / 6 / 0 px) — integer offsets, no easing.
    const int offset = phase == Phase::entering ? frame * 4
                     : phase == Phase::leaving  ? frame * 4 : 0;
    const juce::Rectangle<int> r (0, offset, getWidth(), 40);

    drawBevelBox (g, r, dirt, lava, juce::Colour (0xff3a2410), outline);
    g.setColour (lava);
    g.fillRect (r.getX() + 3, r.getY() + 3, r.getWidth() - 6, 2);
    g.fillRect (r.getX() + 3, r.getBottom() - 5, r.getWidth() - 6, 2);

    drawStar (g, (sparkle / 2) % 2 == 0, r.getX() + 8, r.getY() + 12, 1,
              juce::Colour (0xffffcc33));
    drawPixelText (g, "RECIPE DISCOVERED", r.getX() + 28, r.getY() + 8, 1,
                   juce::Colour (0xffffe98a));
    drawPixelText (g, name, r.getX() + 28, r.getY() + 20, 2, label);
}

// ---- DiscoveriesPanel --------------------------------------------------------

DiscoveriesPanel::DiscoveriesPanel (const RecipeBook& book, DiscoveryStore& store)
    : recipes (book), discoveries (store)
{
    setWantsKeyboardFocus (true);
    closeBtn.setTooltip ("close the book");
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);
}

juce::Rectangle<int> DiscoveriesPanel::panelRect() const
{
    const int n = juce::jmax (1, recipes.getNumRecipes());
    const int rowsPerCol = (n + 1) / 2;
    const int h = 32 + rowsPerCol * 30 + 24;
    return { (getWidth() - 520) / 2, (getHeight() - h) / 2, 520, h };
}

void DiscoveriesPanel::refresh() { repaint(); }

void DiscoveriesPanel::resized()
{
    const auto p = panelRect();
    closeBtn.setBounds (p.getRight() - 24, p.getY() + 4, 16, 16);
}

void DiscoveriesPanel::paint (juce::Graphics& g)
{
    using namespace colours;
    g.fillAll (night.withAlpha (0.78f));

    const auto p = panelRect();
    drawBevelBox (g, p, panelFace, panelLight, panelDark, outline);
    g.setColour (titleBar);
    g.fillRect (p.getX() + 3, p.getY() + 3, p.getWidth() - 6, 18);
    drawPixelText (g, "DISCOVERIES", p.getX() + 8, p.getY() + 8, 1, label);

    const int n = recipes.getNumRecipes();
    const juce::String counter = juce::String (discoveries.getNumFound())
                               + "/" + juce::String (n);
    drawPixelText (g, counter,
                   p.getRight() - 8 - pixelTextWidth (counter, 1) - 24,
                   p.getY() + 8, 1, juce::Colour (0xffffcc33));

    const int rowsPerCol = (n + 1) / 2;
    for (int i = 0; i < n; ++i)
    {
        const int col = i / rowsPerCol, row = i % rowsPerCol;
        const int x = p.getX() + 12 + col * 252;
        const int y = p.getY() + 30 + row * 30;
        const juce::Rectangle<int> slot (x, y, 240, 28);
        drawBevelBox (g, slot, chip, panelLight, panelDark, outline, true);

        const auto& rec = recipes.getRecipe (i);
        const bool found = discoveries.isFound (rec.name);
        drawPixelText (g, juce::String (i + 1), x + 6, y + 10, 1, dimText);
        if (found)
        {
            // The 3x3 pattern at 2x (24px, pure fillRects) — a sharing aid:
            // crisp enough to screenshot and hand to a friend. FOUND only.
            drawMiniCraftIcon (g, rec.pattern, x + 24, y + 2, 2);
            drawStar (g, true, x + 56, y + 6, 1, juce::Colour (0xffffcc33));
            drawPixelText (g, rec.name, x + 74, y + 10, 1, ice);
        }
        else
        {
            drawPixelText (g, "????????????", x + 24, y + 10, 1, panelDark);
        }
    }

    drawPixelTextCentred (g, "HUNT THEM. TRADE THEM. NO HINTS.",
                          { p.getX(), p.getBottom() - 20, p.getWidth(), 8 },
                          1, dimText);
}

void DiscoveriesPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panelRect().contains (e.getPosition()) && onClose)
        onClose();
}

bool DiscoveriesPanel::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::escapeKey) && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

void DiscoveriesPanel::visibilityChanged()
{
    if (isVisible() && isShowing())
        grabKeyboardFocus();
}

// ---- CraftTab ----------------------------------------------------------------

CraftTab::CraftTab (BlockwaveAudioProcessor& processor)
    : proc (processor),
      discoveriesPanel (processor.getRecipeBook(), processor.getDiscoveries())
{
    setSize (kCanvasW, kContentH);

    addAndMakeVisible (gridComp);
    gridComp.onGridEdited = [this] (const CraftGrid& g) { gridEdited (g); };
    gridComp.onSelectionChanged = [this] (int slot)
    {
        if (slot >= 0)
        {
            palette.setArmed (Material::none);       // one aiming mode at a time
            gridComp.setArmedMaterial (Material::none);
        }
        repaint();
    };
    // MIX-knob edits take the weight-only processor path: it re-crafts through
    // the same atomic APVTS write, but never registers a discovery and never
    // touches the recipe name or the auto-name (engine guarantee) — so there
    // is deliberately no refreshLabels() here.
    gridComp.onCellWeightEdited = [this] (int slot, float w)
    {
        proc.setCraftCellWeight (slot, w);
    };

    addAndMakeVisible (palette);
    palette.onTileClicked = [this] (Material m) { tileClicked (m); };

    addAndMakeVisible (keys);
    keys.setLive (false);                            // until setMidiSink() wires it

    addChildComponent (toast);

    diceBtn.setTooltip ("roll random blocks");
    diceBtn.onClick = [this] { doDice(); };
    addAndMakeVisible (diceBtn);

    mutateBtn.setTooltip ("nudge the sound");
    mutateBtn.onClick = [this] { doMutate(); };
    addAndMakeVisible (mutateBtn);

    clearBtn.setTooltip ("empty the bench");
    clearBtn.onClick = [this] { doClear(); };
    addAndMakeVisible (clearBtn);

    basePrev.setTooltip ("previous base");
    basePrev.onClick = [this] { gridComp.cycleBase (-1); };
    addAndMakeVisible (basePrev);

    baseNext.setTooltip ("next base");
    baseNext.onClick = [this] { gridComp.cycleBase (1); };
    addAndMakeVisible (baseNext);

    discoveriesBtn.setSubText ("DISCOVERIES");
    discoveriesBtn.setTooltip ("recipes you found");
    discoveriesBtn.onClick = [this] { showDiscoveries (! isShowingDiscoveries()); };
    addAndMakeVisible (discoveriesBtn);

    addChildComponent (discoveriesPanel);
    discoveriesPanel.setBounds (0, 0, kCanvasW, kContentH);
    discoveriesPanel.onClose = [this] { showDiscoveries (false); };

    refreshFromProcessor();
}

CraftTab::~CraftTab()
{
    stopTimer();
}

void CraftTab::resized()
{
    gridComp.setBounds (kGridX, kGridY, kCraftGridW, kCraftGridW);
    palette.setBounds (kPalX, kPalY, MaterialPalette::width, MaterialPalette::height);
    diceBtn.setBounds (kPalX, kBtnY, kBtnW, kBtnH);
    mutateBtn.setBounds (kPalX + kBtnW + 16, kBtnY, kBtnW, kBtnH);
    discoveriesBtn.setBounds (kCountX, kNameY, kCountW, kNameH);
    basePrev.setBounds (kInfoX + 8, kInfoY + 40, 24, 24);
    baseNext.setBounds (kInfoX + kInfoW - 32, kInfoY + 40, 24, 24);
    clearBtn.setBounds (kInfoX + 8, kInfoY + kInfoH - 40, kInfoW - 16, 24);
    toast.setBounds (kToastX, kToastY, kToastW, kToastH);
    keys.setBounds ((kCanvasW - KeyStrip::width) / 2, kKeysY,
                    KeyStrip::width, KeyStrip::height);
    discoveriesPanel.setBounds (0, 0, getWidth(), getHeight());
}

void CraftTab::visibilityChanged()
{
    if (isVisible())
    {
        refreshFromProcessor();
        startTimerHz (15);                            // house limit is 30 Hz
    }
    else
    {
        stopTimer();
        toast.hideNow();
    }
}

void CraftTab::setMidiSink (std::function<void (int, float)> noteOn,
                            std::function<void (int)> noteOff)
{
    keys.onNoteOn = std::move (noteOn);
    keys.onNoteOff = std::move (noteOff);
    keys.setLive (keys.onNoteOn != nullptr);
}

void CraftTab::showDiscoveries (bool shouldShow)
{
    discoveriesPanel.setVisible (shouldShow);
    if (shouldShow)
    {
        discoveriesPanel.refresh();
        discoveriesPanel.toFront (true);
    }
}

bool CraftTab::isShowingDiscoveries() const { return discoveriesPanel.isVisible(); }

void CraftTab::refreshFromProcessor()
{
    // A preset with NO craft data is not "leave the bench alone" — it is a
    // genuine uncrafted patch, and the bench has to say so. Skipping setGrid
    // in that case left the previous blocks (and any open MIX knob, which is
    // per-block UI state that must never outlive its block) on screen
    // misrepresenting the sound that just loaded, and the 1 s safety net below
    // could not see it either. getCraftGrid leaves `g` default-constructed
    // when it returns false, and a default CraftGrid IS the empty bench, so
    // the unconditional push is also the correct one.
    CraftGrid g;
    hasGrid = proc.getCraftGrid (g);
    gridComp.setGrid (g);
    refreshLabels();
}

void CraftTab::refreshLabels()
{
    patchName = proc.getCraftAutoName().toUpperCase();
    recipeName = proc.getActiveRecipeName().toUpperCase();
    discoveriesBtn.setText (juce::String (proc.getDiscoveries().getNumFound())
                            + "/" + juce::String (proc.getRecipeBook().getNumRecipes()));
    repaint();
}

void CraftTab::gridEdited (const CraftGrid& g)
{
    proc.setCraftGrid (g);                            // craft -> atomic APVTS
    hasGrid = true;
    refreshLabels();
}

void CraftTab::tileClicked (Material m)
{
    const int slot = gridComp.getSelectedSlot();
    if (slot >= 0)
    {
        gridComp.placeMaterial (slot, m);             // cell first, then block
        gridComp.selectSlot (-1);
        palette.setArmed (Material::none);
        repaint();
        return;
    }
    // Nothing selected: arm the block, next cell click places it.
    palette.setArmed (palette.getArmed() == m ? Material::none : m);
    gridComp.setArmedMaterial (palette.getArmed());
    repaint();
}

void CraftTab::doDice()
{
    CraftGrid g;
    if (! proc.getCraftGrid (g))
        proc.setCraftGrid (gridComp.getGrid());       // establish the grid first
    proc.diceCraft();
    refreshFromProcessor();
    gridComp.startDiceAnimation();
    diceBtn.startAnimation (4);
}

void CraftTab::doMutate()
{
    proc.mutateCraft();                               // clears the recipe name
    refreshLabels();
    mutateBtn.startAnimation (4);
    nameGlitch = 3;
}

void CraftTab::doClear()
{
    auto g = gridComp.getGrid();
    for (int i = 0; i < kNumCells; ++i)
    {
        g.cells[i] = Material::none;
        g.setCellWeight (i, kCellWeightDefault);   // an empty bench is 100 %
    }
    gridComp.setGrid (g);
    for (int i = 0; i < kNumCells; ++i)
        gridComp.flashSlot (i);
    gridEdited (g);
    clearBtn.startAnimation (2);
}

void CraftTab::timerCallback()
{
    // 1) discovery poll — the only place the toast and the jingle are
    //    triggered; setCraftGrid is the single discovery entry point.
    juce::String discovered;
    if (proc.consumeRecipeDiscovery (discovered))
    {
        toast.show (discovered);
        refreshLabels();
        if (onDiscovery)
            onDiscovery (discovered);
    }

    // 2) frame-stepped animations (2-4 frames each, no easing).
    gridComp.animationTick();
    diceBtn.animationTick();
    mutateBtn.animationTick();
    clearBtn.animationTick();
    if (discoveriesBtn.getFrame() > 0)
        discoveriesBtn.animationTick();
    toast.animationTick();
    if (nameGlitch > 0)
    {
        --nameGlitch;
        repaint (kNameX, kNameY, kNameW, kNameH);
    }

    // 3) safety net: pick up grids changed elsewhere (preset load through the
    //    top bar, host state restore) about once a second.
    if (--syncCountdown <= 0)
    {
        syncCountdown = 15;
        CraftGrid g;
        const bool valid = proc.getCraftGrid (g);
        // equalsWithWeights, not operator== : a mix edit can change a grid
        // without changing its recipe identity, and a preset load carrying
        // different weights must still resync the bench.
        //
        // The comparison is NOT gated on `valid`: when the processor has no
        // craft, `g` is the empty bench, which is exactly what must be on
        // screen — so the same one-liner catches a no-craft preset load that
        // left stale blocks behind. It is also stable: after the resync the
        // bench IS a default CraftGrid, so the next tick compares equal.
        if (valid != hasGrid
            || ! g.equalsWithWeights (gridComp.getGrid())
            || patchName != proc.getCraftAutoName().toUpperCase())
            refreshFromProcessor();
    }
}

juce::String CraftTab::hintText() const
{
    if (palette.getArmed() != Material::none)
        return juce::String ("CLICK A SLOT TO PLACE ")
               + materialName (palette.getArmed());
    if (gridComp.getSelectedSlot() >= 0)
        return "SLOT PICKED - NOW CLICK A MATERIAL";
    if (! hasGrid)
        return "PLACE A BLOCK TO START CRAFTING";
    for (int i = 0; i < kNumCells; ++i)
        if (gridComp.getGrid().cells[i] != Material::none)
            return "HOVER A BLOCK FOR MIX - RIGHT CLICK CLEARS";
    return "DRAG BLOCKS IN - RIGHT CLICK CLEARS";
}

void CraftTab::paint (juce::Graphics& g)
{
    using namespace colours;

    // ---- big auto-generated patch name ------------------------------------
    const juce::Rectangle<int> plate (kNameX, kNameY, kNameW, kNameH);
    drawBevelBox (g, plate, chip, panelDark, panelLight, outline, true);

    juce::String shown = patchName.isEmpty() ? juce::String ("UNCRAFTED") : patchName;
    if (nameGlitch > 0)                                // MUTATE glitch frames
    {
        for (int i = 0; i < shown.length(); ++i)
            if (glitchRng.nextInt (3) == 0 && shown[i] != ' ')
                shown = shown.replaceSection (i, 1,
                    juce::String::charToString (
                        static_cast<juce::juce_wchar> ('A' + glitchRng.nextInt (26))));
    }

    const bool isRecipe = recipeName.isNotEmpty();
    const juce::Colour nameCol = patchName.isEmpty() ? dimText
                               : isRecipe ? juce::Colour (0xffffcc33) : ice;
    int scale = 3;
    while (scale > 1 && pixelTextWidth (shown, scale) > plate.getWidth() - 48)
        --scale;

    int tx = plate.getX() + 12;
    if (isRecipe)
    {
        drawStar (g, true, tx, plate.getCentreY() - 8, 1, juce::Colour (0xffffcc33));
        tx += 24;
    }
    drawPixelText (g, shown, tx,
                   plate.getCentreY() - pixelTextHeight (scale) / 2, scale, nameCol);

    // ---- bench info panel --------------------------------------------------
    const juce::Rectangle<int> info (kInfoX, kInfoY, kInfoW, kInfoH);
    drawBevelBox (g, info, panelFace, panelLight, panelDark, outline);
    g.setColour (titleBar);
    g.fillRect (info.getX() + 3, info.getY() + 3, info.getWidth() - 6, 16);
    drawPixelText (g, "BENCH", info.getX() + 8, info.getY() + 8, 1, label);

    drawPixelText (g, "BASE", info.getX() + 8, info.getY() + 26, 1, dimText);
    const juce::Rectangle<int> baseBox (info.getX() + 36, info.getY() + 40, 80, 24);
    drawBevelBox (g, baseBox, chip, panelLight, panelDark, outline, true);
    drawPixelTextCentred (g, baseName (gridComp.getGrid().base), baseBox, 1,
                          baseKeyColour (gridComp.getGrid().base));

    int filled = 0;
    for (int i = 0; i < kNumCells; ++i)
        if (gridComp.getGrid().cells[i] != Material::none)
            ++filled;
    drawPixelText (g, "BLOCKS", info.getX() + 8, info.getY() + 76, 1, dimText);
    drawPixelText (g, juce::String (filled) + "/8", info.getX() + 8,
                   info.getY() + 88, 2, filled > 0 ? grass : dimText);

    drawPixelText (g, "RECIPE", info.getX() + 8, info.getY() + 116, 1, dimText);
    if (recipeName.isNotEmpty())
    {
        drawPixelText (g, recipeName, info.getX() + 8, info.getY() + 128, 1,
                       juce::Colour (0xffffcc33));
    }
    else
    {
        drawPixelText (g, "NONE ACTIVE", info.getX() + 8, info.getY() + 128, 1,
                       panelDark);
    }

    // ---- hint line ---------------------------------------------------------
    const juce::Rectangle<int> hint (kHintX, kHintY, kHintW, kHintH);
    drawBevelBox (g, hint, chip, panelDark, panelLight, outline, true);
    drawPixelTextCentred (g, hintText(), hint, 1,
                          palette.getArmed() != Material::none
                              || gridComp.getSelectedSlot() >= 0 ? lava : dimText);

    // ---- keyboard shelf ----------------------------------------------------
    const int kx = (kCanvasW - KeyStrip::width) / 2;
    g.setColour (panelDark);
    g.fillRect (kx - 8, kKeysY - 8, KeyStrip::width + 16, KeyStrip::height + 12);
    g.setColour (outline);
    g.drawRect (kx - 8, kKeysY - 8, KeyStrip::width + 16, KeyStrip::height + 12, 1);
}

} // namespace blockwave::ui
