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
// Third slot on the action row: DICE | MUTATE | CLEAR. 120 wide rather than
// 128, because 696 + 128 would run 8 px past the palette's right edge (816).
// That is the one place the new layout did not fit the fixed canvas at the
// nominal button size — see the note at the top of CraftTab.h.
constexpr int kClearX = kPalX + 2 * (kBtnW + 16), kClearW = 120;
// UNDO / REDO and KEEP live inside the bench info panel, at panel-relative
// 136 and 168 on the 8-px grid. KEEP takes the slot CLEAR used to occupy.
constexpr int kHistY = kInfoY + 136, kHistW = 64, kHistH = 24;
constexpr int kKeepY = kInfoY + 168;
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

// Original 16x16 UNDO arrow: a chunky elbow — a stepped head pointing left,
// a 2-px shaft across the middle, and the shaft's tail dropping away down the
// right. REDO is the same art mirrored on the vertical axis, computed at draw
// time so the pair can never drift apart. Nothing traced, nothing imported.
static const char* const kUndoArrow[16] =
{
    "................",
    "................",
    "................",
    "...11...........",
    "..111...........",
    ".1111...........",
    "1111111111111...",
    "1111111111111...",
    ".1111......11...",
    "..111......11...",
    "...11......11...",
    "...........11...",
    "...........11...",
    "...........11...",
    "................",
    "................",
};

void drawElbowArrow (juce::Graphics& g, bool mirrored, int x, int y, int scale,
                     juce::Colour c)
{
    g.setColour (c);
    for (int r = 0; r < 16; ++r)
        for (int col = 0; col < 16; ++col)
            if (kUndoArrow[r][mirrored ? 15 - col : col] == '1')
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

// ---- base archetype -> preset category ---------------------------------------
// Seven of the eight bases ARE category names. DRONE is the exception: there
// is no DRONE category, and the factory bank already files its DRONE-based
// atmospheres (STRATOSPHERE, CAVE MOUTH) under FX, so a crafted DRONE goes
// there too rather than crowding PAD with things nobody plays chords on.
const char* presetCategoryForBase (blockwave::CraftBase b) noexcept
{
    switch (b)
    {
        case blockwave::CraftBase::LEAD:  return "LEAD";
        case blockwave::CraftBase::BASS:  return "BASS";
        case blockwave::CraftBase::PAD:   return "PAD";
        case blockwave::CraftBase::PLUCK: return "PLUCK";
        case blockwave::CraftBase::KEYS:  return "KEYS";
        case blockwave::CraftBase::CHIP:  return "CHIP";
        case blockwave::CraftBase::PERC:  return "PERC";
        case blockwave::CraftBase::DRONE: return "FX";
        default: break;
    }
    return "LEAD";
}

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

void PixelIconButton::drawGlyph (juce::Graphics& g, int x, int y, int s,
                                 bool dim) const
{
    using namespace colours;
    // A disabled button paints the SAME art in one flat inert grey rather
    // than a faded version of itself: crisp pixels, no alpha, unmistakably
    // off. That is the whole teaching mechanism for UNDO / REDO.
    const juce::Colour offInk { 0xff5c5c64 };
    const auto tint = [dim, offInk] (juce::Colour c) { return dim ? offInk : c; };

    switch (glyph)
    {
        case Glyph::undo:
        case Glyph::redo:
            drawElbowArrow (g, glyph == Glyph::redo, x, y, s, tint (buttonText));
            break;
        case Glyph::dice:
        {
            // Bevelled die; the pip face steps through 4 frames while rolling.
            static const int faceForFrame[4] = { 3, 6, 2, 5 };
            const int face = frame > 0 ? faceForFrame[(frame - 1) % 4] : 5;
            g.setColour (tint (outline));
            g.fillRect (x, y, 16 * s, 16 * s);
            g.setColour (tint (juce::Colour (0xffe8e8f0)));
            g.fillRect (x + s, y + s, 14 * s, 14 * s);
            g.setColour (tint (juce::Colour (0xff9a9aa4)));
            g.fillRect (x + s, y + 13 * s, 14 * s, 2 * s);
            g.setColour (tint (buttonText));
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
            g.setColour (tint (lava));
            g.fillRect (cx - s, cy - step * s, 2 * s, step * 2 * s);
            g.fillRect (cx - step * s, cy - s, step * 2 * s, 2 * s);
            g.setColour (tint (ice));
            const int d = juce::jmax (1, step - 1);
            g.fillRect (cx - d * s, cy - d * s, s, s);
            g.fillRect (cx + (d - 1) * s, cy - d * s, s, s);
            g.fillRect (cx - d * s, cy + (d - 1) * s, s, s);
            g.fillRect (cx + (d - 1) * s, cy + (d - 1) * s, s, s);
            break;
        }
        case Glyph::star:
            drawStar (g, frame % 2 == 0, x, y, s, tint (starGold));
            break;
        case Glyph::arrowLeft:
        case Glyph::arrowRight:
        {
            g.setColour (tint (buttonText));
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
            g.setColour (tint (dirt));
            g.fillRect (x + 7 * s, y + 2 * s, 2 * s, 7 * s);
            g.setColour (tint (buttonText));
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

    // DISABLED: a sunken, flat, colourless slab. Nothing to press, nothing to
    // aim at, and — because JUCE skips disabled components in focus traversal
    // and delivers them no clicks — nothing reachable by mouse or keyboard
    // either. "There is nothing to undo" is stated by the button itself.
    const bool off = ! isEnabled();
    const juce::Colour offFace { 0xff4a4a52 }, offInk { 0xff5c5c64 };

    auto face = buttonFace;
    if (off)              face = offFace;
    else if (down)        face = juce::Colour (0xff6a6a72);
    else if (highlighted) face = juce::Colour (0xff9c9ca4);
    else if (frame > 0)   face = dirt;                  // lit while animating
    drawBevelBox (g, r, face,
                  off ? panelDark : panelLight,
                  off ? outline   : panelDark,
                  outline, down || off);

    const int gs = r.getHeight() >= 40 ? 2 : 1;
    const int gy = r.getY() + (r.getHeight() - 16 * gs) / 2 + (down ? 1 : 0);

    if (text.isEmpty() && subText.isEmpty())
    {
        drawGlyph (g, r.getX() + (r.getWidth() - 16 * gs) / 2, gy, gs, off);
    }
    else
    {
        const int gx = r.getX() + 8;
        if (glyph != Glyph::none)
            drawGlyph (g, gx, gy, gs, off);
        const int tx = gx + (glyph == Glyph::none ? 0 : 16 * gs + 8);
        const int dy = down ? 1 : 0;
        juce::Rectangle<int> textArea (tx, r.getY() + dy,
                                       r.getRight() - 6 - tx, r.getHeight());
        if (subText.isNotEmpty())
        {
            drawPixelText (g, subText, tx, r.getY() + 9 + dy, 1, off ? offInk : chip);
            textArea = { tx, r.getY() + 18 + dy, textArea.getWidth(),
                         r.getHeight() - 18 };
        }
        drawPixelTextCentred (g, text, textArea, textScale,
                              off ? offInk : buttonText);
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

void DiscoveryToast::raise (const juce::String& headlineText,
                            const juce::String& body, juce::Colour accentColour,
                            bool withStar)
{
    headline = headlineText.toUpperCase();
    name = body.toUpperCase();
    accent = accentColour;
    starred = withStar;
    phase = Phase::entering;
    frame = 3;
    holdTicks = 30;                                  // ~2 s at 15 Hz
    sparkle = 0;
    setVisible (true);
    repaint();
}

void DiscoveryToast::show (const juce::String& recipeName)
{
    raise ("RECIPE DISCOVERED", recipeName, colours::lava, true);
}

void DiscoveryToast::showSaved (const juce::String& presetName)
{
    // Gold, because this is the FAVORITES language: the same star the browser
    // rows and the top bar use.
    raise ("KEPT + STARRED", presetName, colours::starGold, true);
}

void DiscoveryToast::showProblem (const juce::String& headlineText,
                                  const juce::String& detail)
{
    raise (headlineText, detail.substring (0, 40), colours::lava, false);
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

    drawBevelBox (g, r, dirt, accent, juce::Colour (0xff3a2410), outline);
    g.setColour (accent);
    g.fillRect (r.getX() + 3, r.getY() + 3, r.getWidth() - 6, 2);
    g.fillRect (r.getX() + 3, r.getBottom() - 5, r.getWidth() - 6, 2);

    const int tx = r.getX() + (starred ? 28 : 8);
    if (starred)
        drawStar (g, (sparkle / 2) % 2 == 0, r.getX() + 8, r.getY() + 12, 1,
                  starGold);
    drawPixelText (g, headline, tx, r.getY() + 8, 1, juce::Colour (0xffffe98a));
    // The body drops to 1x rather than run off the plate — a filesystem error
    // is a long string and it still has to be readable.
    const int bodyScale = pixelTextWidth (name, 2) > r.getWidth() - tx - 8 ? 1 : 2;
    drawPixelText (g, name, tx, r.getY() + 20, bodyScale, label);
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
        weightDragMoved = true;
        // A knob DRAG is one thing the user did, so it is one undo step: the
        // commit waits for the mouse-up below. The wheel and the arrow keys
        // arrive with no gesture around them, so they commit here and the
        // history coalesces a run of them on the same cell.
        if (! gridComp.isMixDragging())
            commitEdit (CraftHistory::Edit::weight, slot);
    };
    // Task 0 + undo framing. begin/endCraftCellWeightGesture turn the whole
    // drag into ONE host parameter gesture (which is what makes FL's "last
    // tweaked" latch and what automation-write records cleanly) instead of a
    // stream of unframed writes.
    gridComp.onCellWeightGestureStart = [this] (int slot)
    {
        proc.beginCraftCellWeightGesture (slot);
        weightDragMoved = false;
        // The state this drag will undo to is the one on screen right now.
        history.rebase (captureCraftState (proc));
    };
    gridComp.onCellWeightGestureEnd = [this] (int slot)
    {
        proc.endCraftCellWeightGesture (slot);
        if (weightDragMoved)                       // a press that never moved
            commitEdit (CraftHistory::Edit::weight, slot);   // is not an edit
        weightDragMoved = false;
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

    undoBtn.setComponentID (kIdUndo);
    undoBtn.setTooltip ("step back once");
    undoBtn.onClick = [this] { undo(); };
    addAndMakeVisible (undoBtn);

    redoBtn.setComponentID (kIdRedo);
    redoBtn.setTooltip ("step forward again");
    redoBtn.onClick = [this] { redo(); };
    addAndMakeVisible (redoBtn);

    keepBtn.setComponentID (kIdKeep);
    keepBtn.setTooltip ("keep this sound");
    keepBtn.onClick = [this] { keepCurrentSound(); };
    addAndMakeVisible (keepBtn);

    addChildComponent (discoveriesPanel);
    discoveriesPanel.setBounds (0, 0, kCanvasW, kContentH);
    discoveriesPanel.onClose = [this] { showDiscoveries (false); };

    refreshFromProcessor();
    history.reset (captureCraftState (proc));         // baseline = what is here
    refreshHistoryButtons();
}

CraftTab::~CraftTab()
{
    stopTimer();
    // An editor torn down mid-drag must not leave a host parameter gesture
    // open. Each call is a no-op when nothing is open for that cell.
    for (int i = 0; i < kNumCells; ++i)
        proc.endCraftCellWeightGesture (i);
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
    clearBtn.setBounds (kClearX, kBtnY, kClearW, kBtnH);
    undoBtn.setBounds (kInfoX + 8, kHistY, kHistW, kHistH);
    redoBtn.setBounds (kInfoX + kInfoW - 8 - kHistW, kHistY, kHistW, kHistH);
    keepBtn.setBounds (kInfoX + 8, kKeepY, kInfoW - 16, kHistH);
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

void CraftTab::presetLoaded()
{
    refreshFromProcessor();
    // New context, clean slate: undo does not reach back across a preset load
    // (rationale in CraftHistory.h).
    history.reset (captureCraftState (proc));
    refreshHistoryButtons();
}

// ---- undo / redo -------------------------------------------------------------

void CraftTab::commitEdit (CraftHistory::Edit kind, int slot)
{
    history.commit (captureCraftState (proc), kind, slot,
                    juce::Time::currentTimeMillis());
    refreshHistoryButtons();
}

void CraftTab::restore (const CraftState& s)
{
    restoreCraftState (proc, s);              // -> setCraftGrid, the write path
    refreshFromProcessor();                   // bench + labels follow it back
    refreshHistoryButtons();
}

void CraftTab::refreshHistoryButtons()
{
    undoBtn.setEnabled (history.canUndo());
    redoBtn.setEnabled (history.canRedo());
    keepBtn.setEnabled (hasGrid);             // nothing crafted, nothing to keep
}

bool CraftTab::undo()
{
    if (isShowingDiscoveries())
        return false;                         // the book owns the keyboard
    const auto* found = history.undo();
    if (found == nullptr)
        return false;
    const CraftState s = *found;              // by value: the ring is about to
    restore (s);                              // be written through by restore
    undoBtn.startAnimation (2);
    return true;
}

bool CraftTab::redo()
{
    if (isShowingDiscoveries())
        return false;
    const auto* found = history.redo();
    if (found == nullptr)
        return false;
    const CraftState s = *found;
    restore (s);
    redoBtn.startAnimation (2);
    return true;
}

// ---- KEEP: save the bench as a user preset AND star it, in one click ---------

bool CraftTab::keepCurrentSound()
{
    if (! hasGrid)
    {
        toast.showProblem ("NOTHING TO KEEP", "PLACE A BLOCK FIRST");
        return false;
    }

    auto& lib = proc.getPresetLibrary();

    // The name the bench is already showing. recipeName is empty after a
    // MUTATE — deliberately, because the sound has left the recipe — so a
    // mutated patch is filed under its material auto-name and never claims a
    // recipe it no longer matches.
    juce::String wanted = recipeName.isNotEmpty() ? recipeName : patchName;
    if (wanted.isEmpty())
        wanted = "BENCH SOUND";

    // COLLISIONS ARE SILENTLY UNIQUIFIED, never prompted: a one-click action
    // that stops to ask a question is not a one-click action, and DICE will
    // hand you the same adjective pair again often enough that a prompt would
    // be the common case rather than the rare one.
    const auto name = lib.makeUniqueName (wanted);
    const juce::String category (presetCategoryForBase (gridComp.getGrid().base));

    juce::String err;
    if (name.isEmpty() || ! proc.saveCurrentAsUserPreset (name, category, err))
    {
        toast.showProblem ("COULD NOT KEEP", err.isEmpty() ? "NO NAME" : err);
        return false;
    }

    // setFavorite, not toggleFavorite: a stale key left behind by a deleted
    // preset of the same name would make a toggle UNSTAR the thing we just
    // saved (see PresetLibrary::setFavorite).
    lib.setFavorite (lib.getCurrentIndex(), true);

    lastKeptName = name;
    lastKeptCategory = category;
    toast.showSaved (name);
    keepBtn.startAnimation (4);
    if (onPresetSaved)                        // top bar + browser follow
        onPresetSaved();
    return true;
}

void CraftTab::refreshLabels()
{
    patchName = proc.getCraftAutoName().toUpperCase();
    recipeName = proc.getActiveRecipeName().toUpperCase();
    discoveriesBtn.setText (juce::String (proc.getDiscoveries().getNumFound())
                            + "/" + juce::String (proc.getRecipeBook().getNumRecipes()));
    keepBtn.setEnabled (hasGrid);
    repaint();
}

void CraftTab::gridEdited (const CraftGrid& g)
{
    proc.setCraftGrid (g);                            // craft -> atomic APVTS
    hasGrid = true;
    refreshLabels();
    commitEdit (CraftHistory::Edit::grid);
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
    commitEdit (CraftHistory::Edit::dice);
    gridComp.startDiceAnimation();
    diceBtn.startAnimation (4);
}

void CraftTab::doMutate()
{
    proc.mutateCraft();                               // clears the recipe name
    refreshLabels();
    // The one edit that moves the parameters without moving the grid, which
    // is why an undo state carries a full ParamSnapshot (see CraftHistory.h).
    commitEdit (CraftHistory::Edit::mutation);
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
    undoBtn.animationTick();
    redoBtn.animationTick();
    keepBtn.animationTick();
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
        {
            refreshFromProcessor();
            // The patch moved without a bench edit — host automation on a
            // craft_mix lane, or a host state restore. REBASE rather than
            // clear: the steps already recorded are still real edits the user
            // made, but the state undo would step back FROM has to be the one
            // that is actually on screen.
            history.rebase (captureCraftState (proc));
            refreshHistoryButtons();
        }
    }
}

// ---- keyboard ----------------------------------------------------------------
//
// A BONUS, not the mechanism: hosts routinely swallow CMD/CTRL+Z before a
// plugin editor ever sees it, so the buttons are the feature and this is what
// works when the host lets it through. Key events reach here by bubbling up
// from whichever child holds focus, and every binding is modified, so the
// bench's own bare-letter and arrow keys (M, RETURN, DELETE, the arrow walk,
// the browser's F) are all untouched.
//
//   CMD/CTRL+Z          undo
//   CMD/CTRL+SHIFT+Z    redo
//   CMD/CTRL+Y          redo (the Windows habit)
bool CraftTab::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (! mods.isCommandDown())
        return false;

    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z')
        return mods.isShiftDown() ? redo() : undo();
    if ((code == 'Y' || code == 'y') && ! mods.isShiftDown())
        return redo();
    return false;
}

// ---- display seams (tools/screenshots, component tests) ----------------------

void CraftTab::dragCellWeightForDisplay (int slot, const float* weights01,
                                         int count)
{
    gridComp.beginMixDrag (slot);
    for (int i = 0; i < count; ++i)
        gridComp.setCellWeightFromDrag (slot, weights01[i]);
    gridComp.endMixDrag();
}

void CraftTab::tickForDisplay (int frames)
{
    for (int i = 0; i < frames; ++i)
        timerCallback();
}

juce::Rectangle<int> CraftTab::getUndoRedoBounds() const
{
    return { kInfoX, kInfoY, kInfoW, kInfoH };
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

    // Shifted up 8 px from the 1.0.0 layout to open the band the UNDO / REDO
    // row now occupies (192..216 in canvas coordinates).
    drawPixelText (g, "RECIPE", info.getX() + 8, info.getY() + 108, 1, dimText);
    if (recipeName.isNotEmpty())
    {
        drawPixelText (g, recipeName, info.getX() + 8, info.getY() + 120, 1,
                       starGold);
    }
    else
    {
        drawPixelText (g, "NONE ACTIVE", info.getX() + 8, info.getY() + 120, 1,
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
