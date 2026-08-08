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

#include "CraftBlocks.h"
#include <cmath>

namespace blockwave::ui
{

juce::String makeMaterialDragId (Material m)
{
    return "M:" + juce::String (static_cast<int> (m));
}

juce::String makeCellDragId (int cellIndex)
{
    return "C:" + juce::String (cellIndex);
}

namespace
{

// Slot layout: the 8 outer cells in reading order (CraftEngine's frozen
// indexing), the base sits in the middle of the 3x3.
//      0 1 2
//      3 . 4
//      5 6 7
juce::Point<int> slotCell (int slot)          // -> column, row
{
    static const int col[kNumCells] = { 0, 1, 2, 0, 2, 0, 1, 2 };
    static const int row[kNumCells] = { 0, 0, 0, 1, 1, 2, 2, 2 };
    return { col[slot], row[slot] };
}

int slotAtCell (int col, int row)             // -1 = base cell / outside
{
    if (col < 0 || col > 2 || row < 0 || row > 2)
        return -2;
    static const int table[3][3] = { { 0, 1, 2 }, { 3, -1, 4 }, { 5, 6, 7 } };
    return table[row][col];
}

Material materialFromDrag (const juce::var& payload, int& fromCell)
{
    fromCell = -1;
    const auto s = payload.toString();
    if (s.startsWith ("M:"))
        return static_cast<Material> (s.substring (2).getIntValue());
    if (s.startsWith ("C:"))
        fromCell = s.substring (2).getIntValue();
    return Material::none;
}

// Linear walk order for LEFT/RIGHT: the 3x3 in reading order with the base
// in the middle (-1). UP/DOWN are given over to the mix while a cell's knob is
// open, so this single axis has to reach every cell on its own — hence the wrap.
const int kWalkOrder[9] = { 0, 1, 2, 3, -1, 4, 5, 6, 7 };

int walkIndexOf (int slot)
{
    for (int i = 0; i < 9; ++i)
        if (kWalkOrder[i] == slot)
            return i;
    return 4;                                     // base
}

// Mix weights live on a 5 % lattice: round readouts, 21 reachable values.
float snapMixWeight (float w)
{
    const float snapped = std::round (juce::jlimit (0.0f, 1.0f, w) / kMixStep) * kMixStep;
    return juce::jlimit (0.0f, 1.0f, snapped);
}

juce::String mixPercentText (float w)
{
    return juce::String (juce::roundToInt (w * 100.0f)) + "%";
}

// ---- hidden MIX art ---------------------------------------------------------

// The MIX label carries no plate and no outline (producer spec: "just letters
// in a muted colour"), so its contrast has to come from the block underneath.
// Muted is the same neutral mid grey on every material — genuinely quiet on a
// light block and on a dark one. The HOT tone is the block's own opposite
// extreme, so hovering the letters is a full jump in contrast either way
// (mid grey -> near white on a dark block, mid grey -> near black on a light
// one) instead of a shade nobody can see.
struct MixLabelTones { juce::Colour muted, strong; };

MixLabelTones mixLabelTones (Material m, float weight01)
{
    const auto tone = materialKeyColour (m, weightDimLevel (weight01));
    return { colours::panelFace,
             tone.getPerceivedBrightness() < 0.5f ? colours::label
                                                  : colours::outline };
}

// 24x24 stepped knob, 16 frames, drawn from scratch on the pixel grid: outline
// ring, bevelled collar, flat face, 1-px ice pointer. Round, so the block art
// stays visible in the corners — the knob sits ON the block, it is not a plate.
juce::Image buildMixKnobStrip()
{
    using namespace colours;
    juce::Image strip (juce::Image::ARGB, kMixKnobFrames * kMixKnobArt,
                       kMixKnobArt, true);
    const juce::Colour collarLight { 0xffb2b2ba };
    const juce::Colour collarDark  { 0xff565660 };
    const juce::Colour face        { 0xff7e7e88 };
    const float centre = static_cast<float> (kMixKnobArt) / 2.0f - 0.5f;

    for (int f = 0; f < kMixKnobFrames; ++f)
    {
        const float pos = static_cast<float> (f)
                        / static_cast<float> (kMixKnobFrames - 1);
        const float ang = juce::MathConstants<float>::pi * (-0.75f + 1.5f * pos);
        const int ox = f * kMixKnobArt;

        for (int y = 0; y < kMixKnobArt; ++y)
            for (int x = 0; x < kMixKnobArt; ++x)
            {
                const float dx = static_cast<float> (x) - centre;
                const float dy = static_cast<float> (y) - centre;
                const float r = std::sqrt (dx * dx + dy * dy);
                if (r > 11.2f)
                    continue;                       // transparent: block shows
                juce::Colour c;
                if (r > 9.4f)
                    c = outline;                    // 2-px dark rim
                else if (r > 6.5f)
                    c = (dx + dy < -2.0f) ? collarLight
                      : (dx + dy >  2.0f) ? collarDark : stone;
                else
                    c = face;
                strip.setPixelAt (ox + x, y, c);
            }

        for (float rr = 2.0f; rr <= 8.6f; rr += 0.5f)
            strip.setPixelAt (ox + static_cast<int> (std::lround (centre + std::sin (ang) * rr)),
                              static_cast<int> (std::lround (centre - std::cos (ang) * rr)),
                              colours::ice);
    }
    return strip;
}

} // namespace

// ---- CraftCell --------------------------------------------------------------

CraftCell::CraftCell (CraftGridComponent& owner, int slotIndex, bool isBaseSlot)
    : grid (owner), slot (slotIndex), baseSlot (isBaseSlot)
{
    setWantsKeyboardFocus (true);
    setSize (kCraftCell, kCraftCell);
}

void CraftCell::paint (juce::Graphics& g)
{
    using namespace colours;
    const auto r = getLocalBounds();

    if (baseSlot)
    {
        const auto base = grid.getGrid().base;
        drawBevelBox (g, r, titleBar, panelFace, outline, outline);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageAt (grid.getCache().base (base, 3), r.getX() + 8, r.getY() + 4);
        drawPixelTextCentred (g, baseName (base),
                              { r.getX(), r.getBottom() - 12, r.getWidth(), 6 },
                              1, baseKeyColour (base));
    }
    else
    {
        const auto m = grid.displayedMaterial (slot);
        const float weight = grid.cellWeight (slot);
        if (m == Material::none)
        {
            drawBevelBox (g, r, chip, panelLight, panelDark, outline, true);
            // Empty-slot crosshair: four pips around the centre, nothing fancy.
            g.setColour (panelDark);
            const int cx = r.getCentreX() - 2, cy = r.getCentreY() - 2;
            g.fillRect (cx - 4, cy, 4, 4);
            g.fillRect (cx + 4, cy, 4, 4);
            g.fillRect (cx, cy - 4, 4, 4);
            g.fillRect (cx, cy + 4, 4, 4);
        }
        else
        {
            // Cached sprite, pre-dimmed for this cell's mix level — flat
            // indexed pixel art either way, nothing translucent.
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImageAt (grid.getCache().material (m, 4, weightDimLevel (weight)),
                           r.getX(), r.getY());
        }

        // ---- hidden MIX: knob first (it is under the label), then the
        // letters, then the persistent nn% tag. Nothing at all at rest.
        if (hasMixControl())
        {
            if (isMixOpen())
                paintMixKnob (g, weight);
            if (hovering || labelHot || labelArmed || isMixOpen()
                || hasKeyboardFocus (false))
                paintMixLabel (g, m, weight);
            if (weight < 1.0f || isMixOpen())
                paintMixTag (g, weight);
        }

        if (const int f = grid.slotFlash (slot); f > 0)
        {
            g.setColour (f > 1 ? juce::Colours::white : colours::ice);
            g.drawRect (r, 2);                    // 2-frame place flash
        }
    }

    if (dragOver)
    {
        g.setColour (colours::ice);
        g.drawRect (r, 2);
    }
    else if (grid.getSelectedSlot() == slot && ! baseSlot)
    {
        g.setColour (colours::lava);
        g.drawRect (r, 2);
    }
    else if (hovering)
    {
        g.setColour (colours::panelLight);
        g.drawRect (r, 1);
    }

    if (hasMixControl() && grid.mixReadoutSlot() == slot)
        paintMixReadout (g, grid.cellWeight (slot));

    if (hasKeyboardFocus (false))
        drawFocusTicks (g, r);
}

// ---- hidden MIX --------------------------------------------------------------

bool CraftCell::hasMixControl() const
{
    return ! baseSlot && slot >= 0 && slot < kNumCells
        && grid.getGrid().cells[slot] != Material::none;
}

bool CraftCell::isMixOpen() const
{
    return hasMixControl() && grid.isMixKnobOpen (slot);
}

// Top-right corner. Owns the click, never the drag (see mouseDrag).
juce::Rectangle<int> CraftCell::mixLabelBounds() const
{
    return { getWidth() - kMixLabelW, 0, kMixLabelW, kMixLabelH };
}

// Bottom-right corner, live only while the knob is open. Everything outside
// it still starts a block drag-and-drop.
juce::Rectangle<int> CraftCell::mixKnobBounds() const
{
    return { getWidth() - kMixKnobBox, getHeight() - kMixKnobBox,
             kMixKnobBox, kMixKnobBox };
}

void CraftCell::paintMixLabel (juce::Graphics& g, Material m, float w)
{
    using namespace colours;
    const auto tones = mixLabelTones (m, w);
    const bool open = isMixOpen();
    const auto colour = open ? (labelHot ? lava.brighter (0.45f) : lava)
                             : (labelHot ? tones.strong : tones.muted);

    // Bare letters: no plate, no bevel, no outline (producer spec).
    const auto box = mixLabelBounds();
    drawPixelText (g, "MIX", box.getRight() - 3 - pixelTextWidth ("MIX", 1),
                   box.getY() + 4, 1, colour);
}

void CraftCell::paintMixKnob (juce::Graphics& g, float w)
{
    const auto box = mixKnobBounds();
    const int frame = juce::jlimit (0, kMixKnobFrames - 1,
        static_cast<int> (std::lround (juce::jlimit (0.0f, 1.0f, w)
                          * static_cast<float> (kMixKnobFrames - 1))));
    const int dx = box.getCentreX() - kMixKnobArt / 2;
    const int dy = box.getCentreY() - kMixKnobArt / 2;

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (grid.mixKnobStrip(), dx, dy, kMixKnobArt, kMixKnobArt,
                 frame * kMixKnobArt, 0, kMixKnobArt, kMixKnobArt);

    if (mixDragging)                     // grabbed: lava corner ticks, no glow
    {
        g.setColour (colours::lava);
        const int t = 3;
        g.fillRect (box.getX(), box.getY(), t, 1);
        g.fillRect (box.getX(), box.getY(), 1, t);
        g.fillRect (box.getRight() - t, box.getBottom() - 1, t, 1);
        g.fillRect (box.getRight() - 1, box.getBottom() - t, 1, t);
    }
}

// Persistent "this block is turned down" tag: bottom-left, out of the
// sprite's busiest area. Always shown while the knob is open so you can read
// what you are dialling, otherwise only below 100 %.
void CraftCell::paintMixTag (juce::Graphics& g, float w)
{
    using namespace colours;
    const auto txt = mixPercentText (w);
    const juce::Rectangle<int> tag (2, getHeight() - 11,
                                    pixelTextWidth (txt, 1) + 5, 9);
    drawBevelBox (g, tag, chip, panelDark, panelLight, outline, true, 1);
    drawPixelTextCentred (g, txt, tag, 1,
                          w >= 1.0f ? grass : (w > 0.0f ? ice : lava));
}

void CraftCell::paintMixReadout (juce::Graphics& g, float w)
{
    using namespace colours;
    const auto txt = mixPercentText (w);
    const int tw = pixelTextWidth (txt, 2);
    juce::Rectangle<int> badge (0, 0, tw + 12, 20);
    // Parked up-left of the knob so a drag never hides the control doing it.
    badge.setCentre (getWidth() / 2 - 2, getHeight() / 2 - 8);
    drawBevelBox (g, badge, chip, lava, juce::Colour (0xff3a2410), outline);
    drawPixelTextCentred (g, txt, badge, 2, label);
}

void CraftCell::setHoverForDisplay (bool overCell, bool overLabel)
{
    hovering = overCell;
    labelHot = overLabel && hasMixControl();
    repaint();
}

// ---- mouse -------------------------------------------------------------------

void CraftCell::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mixDragging = false;
    labelArmed = false;
    if (baseSlot)
    {
        grid.cycleBase (e.mods.isPopupMenu() ? -1 : 1);
        return;
    }

    // Right-click and an ARMED material win over everything and apply to
    // every pixel of the cell — label and knob included, so neither the
    // clear gesture nor the guided click-cell-then-click-material flow ever
    // hits a dead zone.
    if (e.mods.isPopupMenu() || grid.getArmedMaterial() != Material::none)
    {
        grid.cellClicked (slot, e.mods.isPopupMenu());
        return;
    }

    if (hasMixControl())
    {
        // The knob only exists while it is open, and only owns its own box.
        if (isMixOpen() && mixKnobBounds().contains (e.getPosition()))
        {
            mixDragging = true;
            mixDragStartY = e.getPosition().y;
            mixDragStartWeight = grid.cellWeight (slot);
            grid.beginMixDrag (slot);             // one framed host gesture
            repaint();
            return;
        }
        // The label ARMS here and toggles on mouseUp; travelling turns the
        // same press into a normal block move (see mouseDrag).
        if (mixLabelBounds().contains (e.getPosition()))
        {
            labelArmed = true;
            repaint();
            return;
        }
    }
    grid.cellClicked (slot, false);
}

void CraftCell::mouseDrag (const juce::MouseEvent& e)
{
    if (baseSlot || e.mods.isPopupMenu())
        return;

    if (mixDragging)
    {
        // kMixDragTravel px of travel = the full 0..100 % range, up = louder.
        const float delta = static_cast<float> (mixDragStartY - e.getPosition().y)
                          / static_cast<float> (kMixDragTravel);
        grid.setCellWeightFromDrag (slot, mixDragStartWeight + delta);
        return;
    }

    const auto m = grid.getGrid().cells[slot];
    if (m == Material::none || e.getDistanceFromDragStart() < 4)
        return;

    // A press that started on the MIX label but then travelled is a block
    // move, not a toggle: the label must never cost you the drag gesture.
    if (labelArmed)
    {
        labelArmed = false;
        repaint();
    }

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        const juce::ScaledImage img (makeMaterialImage (m, 3), 1.0);
        const juce::Point<int> offset (-24, -24);
        container->startDragging (makeCellDragId (slot), this, img, false, &offset);
    }
}

void CraftCell::mouseUp (const juce::MouseEvent& e)
{
    if (mixDragging)
    {
        mixDragging = false;
        grid.endMixDrag();
        repaint();
        return;
    }
    if (labelArmed)
    {
        labelArmed = false;
        if (mixLabelBounds().contains (e.getPosition()))
            grid.toggleMixKnob (slot);            // open on click, hide on the next
        else
            repaint();                            // released off the label: nothing
    }
}

void CraftCell::mouseMove (const juce::MouseEvent& e)
{
    const bool hot = hasMixControl() && mixLabelBounds().contains (e.getPosition());
    if (hot != labelHot)
    {
        labelHot = hot;
        repaint (mixLabelBounds());
    }
}

void CraftCell::mouseWheelMove (const juce::MouseEvent&,
                                const juce::MouseWheelDetails& wheel)
{
    // Only over an OPEN knob: with no control on screen there are no
    // invisible edits.
    if (! isMixOpen() || wheel.deltaY == 0.0f)
        return;
    grid.nudgeCellWeight (slot, wheel.deltaY > 0.0f ? 1 : -1);
}

void CraftCell::mouseEnter (const juce::MouseEvent& e)
{
    hovering = true;
    labelHot = hasMixControl() && mixLabelBounds().contains (e.getPosition());
    repaint();
}

void CraftCell::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    labelHot = false;
    labelArmed = false;
    repaint();
}

bool CraftCell::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();

    // M is the keyboard twin of the MIX label: it toggles the knob on the
    // focused block. The label is drawn whenever a cell has keyboard focus,
    // so the shortcut has something to point at.
    if (juce::CharacterFunctions::toUpperCase (k.getTextCharacter()) == 'M'
        && ! k.getModifiers().isCommandDown())
    {
        if (! hasMixControl())
            return true;                           // empty cell: nothing to mix
        grid.toggleMixKnob (slot);
        return true;
    }

    // LEFT/RIGHT walk the WHOLE grid in reading order and wrap.
    if (code == juce::KeyPress::leftKey)  { grid.focusStep (slot, -1); return true; }
    if (code == juce::KeyPress::rightKey) { grid.focusStep (slot,  1); return true; }

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const int dir = code == juce::KeyPress::upKey ? 1 : -1;
        // The arrows belong to the mix ONLY while this cell's knob is open;
        // the rest of the time they walk the bench by rows, as they always
        // did on empty and base cells.
        if (isMixOpen())
        {
            grid.nudgeCellWeight (slot, dir * (k.getModifiers().isShiftDown() ? 5 : 1));
            return true;
        }
        grid.focusNeighbour (slot, 0, -dir);
        return true;
    }

    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
    {
        if (baseSlot)
            grid.cycleBase (1);
        else
            grid.cellClicked (slot, false);
        return true;
    }
    if (! baseSlot && (code == juce::KeyPress::deleteKey
                       || code == juce::KeyPress::backspaceKey))
    {
        grid.clearSlot (slot);
        return true;
    }
    return false;
}

bool CraftCell::isInterestedInDragSource (const SourceDetails& d)
{
    if (baseSlot)
        return false;
    const auto s = d.description.toString();
    return s.startsWith ("M:") || s.startsWith ("C:");
}

void CraftCell::itemDragEnter (const SourceDetails&) { dragOver = true;  repaint(); }
void CraftCell::itemDragExit  (const SourceDetails&) { dragOver = false; repaint(); }

void CraftCell::itemDropped (const SourceDetails& d)
{
    dragOver = false;
    int fromCell = -1;
    const auto m = materialFromDrag (d.description, fromCell);
    if (fromCell >= 0)
        grid.moveMaterial (fromCell, slot);
    else if (m != Material::none)
        grid.placeMaterial (slot, m);
    repaint();
}

// ---- CraftGridComponent ------------------------------------------------------

CraftGridComponent::CraftGridComponent (BlockImageCache& imageCache)
    : cache (imageCache), rng (0x51ce)
{
    for (int s = 0; s < kNumCells; ++s)
    {
        auto cell = std::make_unique<CraftCell> (*this, s, false);
        addAndMakeVisible (*cell);
        cells.push_back (std::move (cell));
    }
    auto baseCell = std::make_unique<CraftCell> (*this, -1, true);
    baseCell->setTooltip (baseTooltip (grid.base));
    addAndMakeVisible (*baseCell);
    cells.push_back (std::move (baseCell));

    refreshCellTooltips();
    setSize (kCraftGridW, kCraftGridW);
}

void CraftGridComponent::refreshCellTooltips()
{
    // 3 words max, product voice: a filled block advertises both of its
    // gestures, an empty one advertises the drop.
    for (int s = 0; s < kNumCells; ++s)
        cells[static_cast<size_t> (s)]->setTooltip (
            grid.cells[s] == Material::none ? "drop a block" : "drag or mix");
}

// ---- expanded mix knobs (pure UI state) --------------------------------------

bool CraftGridComponent::isMixKnobOpen (int slot) const noexcept
{
    return slot >= 0 && slot < kNumCells && mixOpen[slot];
}

void CraftGridComponent::setMixKnobOpen (int slot, bool shouldBeOpen)
{
    if (slot < 0 || slot >= kNumCells)
        return;
    if (shouldBeOpen && grid.cells[slot] == Material::none)
        return;                                   // empty cell: nothing to mix
    if (mixOpen[slot] == shouldBeOpen)
        return;
    mixOpen[slot] = shouldBeOpen;
    cells[static_cast<size_t> (slot)]->repaint();
}

void CraftGridComponent::toggleMixKnob (int slot)
{
    setMixKnobOpen (slot, ! isMixKnobOpen (slot));
}

void CraftGridComponent::setHoverForDisplay (int slot, bool overCell,
                                             bool overLabel)
{
    if (slot < 0 || slot >= kNumCells)
        return;
    cells[static_cast<size_t> (slot)]->setHoverForDisplay (overCell, overLabel);
}

const juce::Image& CraftGridComponent::mixKnobStrip()
{
    if (! knobStrip.isValid())
        knobStrip = buildMixKnobStrip();
    return knobStrip;
}

void CraftGridComponent::resized()
{
    for (int s = 0; s < kNumCells; ++s)
    {
        const auto cr = slotCell (s);
        cells[static_cast<size_t> (s)]->setBounds (cr.x * (kCraftCell + kCraftGap),
                                                   cr.y * (kCraftCell + kCraftGap),
                                                   kCraftCell, kCraftCell);
    }
    cells.back()->setBounds (kCraftCell + kCraftGap, kCraftCell + kCraftGap,
                             kCraftCell, kCraftCell);
}

void CraftGridComponent::paint (juce::Graphics& g)
{
    // Bench plate behind the cells (the 8-px gaps read as grout).
    using namespace colours;
    g.setColour (panelDark);
    g.fillRect (getLocalBounds());
    g.setColour (outline);
    g.drawRect (getLocalBounds(), 1);
}

void CraftGridComponent::setGrid (const CraftGrid& g)
{
    grid = g;
    cells.back()->setTooltip (baseTooltip (grid.base));
    refreshCellTooltips();
    readoutSlot = -1;                            // no stale % badge on reload
    readoutTicks = 0;
    readoutHeld = false;
    // The whole bench was replaced: every expanded knob belonged to a block
    // that is no longer necessarily there. Expanded state is per-cell UI
    // state and is never restored from a preset.
    for (int s = 0; s < kNumCells; ++s)
        mixOpen[s] = false;
    for (auto& c : cells)
        c->repaint();
}

void CraftGridComponent::notifyEdit()
{
    if (onGridEdited)
        onGridEdited (grid);
}

void CraftGridComponent::selectSlot (int slot)
{
    if (selected == slot)
        return;
    selected = slot;
    for (auto& c : cells)
        c->repaint();
    if (onSelectionChanged)
        onSelectionChanged (selected);
}

void CraftGridComponent::placeMaterial (int slot, Material m)
{
    if (slot < 0 || slot >= kNumCells || grid.cells[slot] == m)
    {
        if (slot >= 0 && slot < kNumCells)
            flashSlot (slot);
        return;
    }
    grid.cells[slot] = m;
    // A NEW block always arrives at 100 % with its knob closed: the mix (and
    // the fact that you were fiddling with it) belongs to the block you
    // placed, never to the slot it landed in.
    grid.setCellWeight (slot, kCellWeightDefault);
    mixOpen[slot] = false;
    flashSlot (slot);
    refreshCellTooltips();
    cells[static_cast<size_t> (slot)]->repaint();
    notifyEdit();
}

void CraftGridComponent::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumCells || grid.cells[slot] == Material::none)
        return;
    grid.cells[slot] = Material::none;
    grid.setCellWeight (slot, kCellWeightDefault);   // block gone, mix gone
    mixOpen[slot] = false;                           // ...and the knob with it
    if (readoutSlot == slot)
        readoutSlot = -1;
    flashSlot (slot);
    refreshCellTooltips();
    cells[static_cast<size_t> (slot)]->repaint();
    notifyEdit();
}

void CraftGridComponent::moveMaterial (int fromSlot, int toSlot)
{
    if (fromSlot == toSlot || fromSlot < 0 || fromSlot >= kNumCells
        || toSlot < 0 || toSlot >= kNumCells)
        return;
    std::swap (grid.cells[fromSlot], grid.cells[toSlot]);   // swap keeps blocks
    // ...and each block keeps its own mix: dragging a 40 % ICE across the
    // bench must not silently turn it back up.
    const float wFrom = grid.cellWeight (fromSlot);
    grid.setCellWeight (fromSlot, grid.cellWeight (toSlot));
    grid.setCellWeight (toSlot, wFrom);
    // The expanded knob travels with its block too, and a cell left empty by
    // the swap can never keep one.
    std::swap (mixOpen[fromSlot], mixOpen[toSlot]);
    for (int s : { fromSlot, toSlot })
        if (grid.cells[s] == Material::none)
            mixOpen[s] = false;
    flashSlot (toSlot);
    flashSlot (fromSlot);
    refreshCellTooltips();
    cells[static_cast<size_t> (fromSlot)]->repaint();
    cells[static_cast<size_t> (toSlot)]->repaint();
    notifyEdit();
}

// ---- per-cell WEIGHT / MIX ---------------------------------------------------

float CraftGridComponent::cellWeight (int slot) const
{
    return grid.cellWeight (slot);
}

void CraftGridComponent::applyCellWeight (int slot, float w, bool hold)
{
    if (slot < 0 || slot >= kNumCells || grid.cells[slot] == Material::none)
        return;

    readoutSlot = slot;
    readoutHeld = hold;
    readoutTicks = hold ? 0 : kMixReadoutTicks;

    const float snapped = snapMixWeight (w);
    if (grid.cellWeight (slot) != snapped)
    {
        grid.setCellWeight (slot, snapped);
        if (onCellWeightEdited)
            onCellWeightEdited (slot, snapped);   // -> setCraftCellWeight()
    }
    cells[static_cast<size_t> (slot)]->repaint();
}

void CraftGridComponent::beginMixDrag (int slot)
{
    if (slot < 0 || slot >= kNumCells)
        return;
    mixDragSlot = slot;
    if (onCellWeightGestureStart)
        onCellWeightGestureStart (slot);
}

void CraftGridComponent::setCellWeightFromDrag (int slot, float w)
{
    applyCellWeight (slot, w, true);
}

void CraftGridComponent::endMixDrag()
{
    // Keyed on the DRAG, not on the readout: a press-and-release that never
    // travelled sets no readout at all, and closing its gesture still matters
    // — an unclosed begin/endChangeGesture pair leaves the host recording.
    const int slot = mixDragSlot;
    mixDragSlot = -1;

    if (readoutHeld)
    {
        readoutHeld = false;
        readoutTicks = kMixReadoutTicks;          // lingers, then fades out
    }

    if (slot >= 0 && onCellWeightGestureEnd)
        onCellWeightGestureEnd (slot);
}

void CraftGridComponent::nudgeCellWeight (int slot, int steps)
{
    if (slot < 0 || slot >= kNumCells || steps == 0)
        return;
    applyCellWeight (slot,
                     grid.cellWeight (slot) + static_cast<float> (steps) * kMixStep,
                     false);
}

void CraftGridComponent::focusStep (int slot, int delta)
{
    const int here = walkIndexOf (slot);
    const int next = ((here + delta) % 9 + 9) % 9;      // wraps across rows
    const int target = kWalkOrder[next];
    const size_t idx = target < 0 ? cells.size() - 1 : static_cast<size_t> (target);
    cells[idx]->grabKeyboardFocus();
}

void CraftGridComponent::cycleBase (int delta)
{
    const int n = kNumBases;
    const int next = ((static_cast<int> (grid.base) + delta) % n + n) % n;
    grid.base = static_cast<CraftBase> (next);
    cells.back()->setTooltip (baseTooltip (grid.base));
    cells.back()->repaint();
    notifyEdit();
}

void CraftGridComponent::cellClicked (int slot, bool isRightClick)
{
    if (isRightClick)
    {
        clearSlot (slot);
        selectSlot (-1);
        return;
    }
    if (armed != Material::none)
    {
        placeMaterial (slot, armed);              // armed material -> cell
        return;
    }
    selectSlot (selected == slot ? -1 : slot);    // cell -> then pick material
}

void CraftGridComponent::focusNeighbour (int slot, int dx, int dy)
{
    juce::Point<int> here = slot < 0 ? juce::Point<int> (1, 1) : slotCell (slot);
    const int col = juce::jlimit (0, 2, here.x + dx);
    const int row = juce::jlimit (0, 2, here.y + dy);
    const int target = slotAtCell (col, row);
    if (target == -2)
        return;
    const size_t idx = target < 0 ? cells.size() - 1 : static_cast<size_t> (target);
    cells[idx]->grabKeyboardFocus();
}

void CraftGridComponent::startDiceAnimation()
{
    diceFrames = 3;                                // 3 scramble frames + result
    for (int s = 0; s < kNumCells; ++s)
        scramble[s] = static_cast<Material> (1 + rng.nextInt (kNumMaterials));
    for (auto& c : cells)
        c->repaint();
}

void CraftGridComponent::flashSlot (int slot)
{
    if (slot >= 0 && slot < kNumCells)
    {
        flash[slot] = 2;
        cells[static_cast<size_t> (slot)]->repaint();
    }
}

bool CraftGridComponent::animationTick()
{
    bool busy = false;

    // Mix readout: held while the knob is being dragged, then a short linger
    // so a keyboard/wheel nudge is actually readable before it goes.
    if (readoutSlot >= 0 && ! readoutHeld && readoutTicks > 0)
    {
        busy = true;
        if (--readoutTicks <= 0)
        {
            const int done = readoutSlot;
            readoutSlot = -1;
            cells[static_cast<size_t> (done)]->repaint();
        }
    }

    const bool dicing = diceFrames > 0;
    if (dicing)
    {
        --diceFrames;
        for (int s = 0; s < kNumCells; ++s)
            scramble[s] = static_cast<Material> (1 + rng.nextInt (kNumMaterials));
        busy = true;
    }
    for (int s = 0; s < kNumCells; ++s)
    {
        bool dirty = dicing;
        if (flash[s] > 0)
        {
            --flash[s];
            busy = dirty = true;
        }
        if (dirty)
            cells[static_cast<size_t> (s)]->repaint();
    }
    return busy;
}

Material CraftGridComponent::displayedMaterial (int slot) const
{
    if (slot < 0 || slot >= kNumCells)
        return Material::none;
    return diceFrames > 0 ? scramble[slot] : grid.cells[slot];
}

int CraftGridComponent::slotFlash (int slot) const
{
    return slot >= 0 && slot < kNumCells ? flash[slot] : 0;
}

// ---- MaterialTile ------------------------------------------------------------

MaterialTile::MaterialTile (MaterialPalette& owner, Material m)
    : palette (owner), material (m)
{
    setWantsKeyboardFocus (true);
    setTooltip (materialTooltip (m));
    setSize (kPaletteTileW, kPaletteTileH);
}

void MaterialTile::paint (juce::Graphics& g)
{
    using namespace colours;
    const auto r = getLocalBounds();
    const bool armed = palette.isArmed (material);

    drawBevelBox (g, r, armed ? dirt : titleBar,
                  armed ? lava : panelDark, outline, outline, ! armed);

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageAt (palette.getCache().material (material, 2),
                   r.getX() + (r.getWidth() - 32) / 2, r.getY() + 3);

    drawPixelTextCentred (g, materialName (material),
                          { r.getX(), r.getBottom() - 11, r.getWidth(), 6 }, 1,
                          armed ? label : dimText);

    if (hovering && ! armed)
    {
        g.setColour (panelLight);
        g.drawRect (r, 1);
    }
    if (hasKeyboardFocus (false))
        drawFocusTicks (g, r);
}

void MaterialTile::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
    dragging = false;
}

void MaterialTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 4)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        const juce::ScaledImage img (makeMaterialImage (material, 3), 1.0);
        const juce::Point<int> offset (-24, -24);
        container->startDragging (makeMaterialDragId (material), this, img, false, &offset);
    }
}

void MaterialTile::mouseUp (const juce::MouseEvent& e)
{
    if (dragging || ! getLocalBounds().contains (e.getPosition()))
        return;
    if (palette.onTileClicked)
        palette.onTileClicked (material);
}

void MaterialTile::mouseEnter (const juce::MouseEvent&) { hovering = true;  repaint(); }
void MaterialTile::mouseExit  (const juce::MouseEvent&) { hovering = false; repaint(); }

bool MaterialTile::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::returnKey) || k.isKeyCode (juce::KeyPress::spaceKey))
    {
        if (palette.onTileClicked)
            palette.onTileClicked (material);
        return true;
    }
    return false;
}

// ---- MaterialPalette ---------------------------------------------------------

MaterialPalette::MaterialPalette (BlockImageCache& imageCache) : cache (imageCache)
{
    for (int i = 1; i <= kNumMaterials; ++i)
    {
        auto tile = std::make_unique<MaterialTile> (*this, static_cast<Material> (i));
        addAndMakeVisible (*tile);
        tiles.push_back (std::move (tile));
    }
    setSize (width, height);
}

void MaterialPalette::setArmed (Material m)
{
    if (armed == m)
        return;
    armed = m;
    for (auto& t : tiles)
        t->repaint();
}

void MaterialPalette::resized()
{
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const int col = static_cast<int> (i) % kCols;
        const int row = static_cast<int> (i) / kCols;
        tiles[i]->setBounds (12 + col * (kPaletteTileW + 8),
                             24 + row * (kPaletteTileH + 8),
                             kPaletteTileW, kPaletteTileH);
    }
}

void MaterialPalette::paint (juce::Graphics& g)
{
    using namespace colours;
    const auto r = getLocalBounds();
    drawBevelBox (g, r, panelFace, panelLight, panelDark, outline);
    g.setColour (titleBar);
    g.fillRect (r.getX() + 3, r.getY() + 3, r.getWidth() - 6, 16);
    drawPixelText (g, "MATERIALS", r.getX() + 8, r.getY() + 8, 1, label);
    drawPixelText (g, armed == Material::none ? "DRAG OR CLICK A BLOCK"
                                              : juce::String ("ARMED: ")
                                                    + materialName (armed),
                   r.getX() + 96, r.getY() + 8, 1,
                   armed == Material::none ? dimText : lava);
}

} // namespace blockwave::ui
