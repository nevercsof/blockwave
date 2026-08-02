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
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImageAt (grid.getCache().material (m, 4), r.getX(), r.getY());
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

    if (hasKeyboardFocus (false))
        drawFocusTicks (g, r);
}

void CraftCell::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (baseSlot)
    {
        grid.cycleBase (e.mods.isPopupMenu() ? -1 : 1);
        return;
    }
    grid.cellClicked (slot, e.mods.isPopupMenu());
}

void CraftCell::mouseDrag (const juce::MouseEvent& e)
{
    if (baseSlot || e.mods.isPopupMenu())
        return;
    const auto m = grid.getGrid().cells[slot];
    if (m == Material::none || e.getDistanceFromDragStart() < 4)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        const juce::ScaledImage img (makeMaterialImage (m, 3), 1.0);
        const juce::Point<int> offset (-24, -24);
        container->startDragging (makeCellDragId (slot), this, img, false, &offset);
    }
}

void CraftCell::mouseEnter (const juce::MouseEvent&) { hovering = true;  repaint(); }
void CraftCell::mouseExit  (const juce::MouseEvent&) { hovering = false; repaint(); }

bool CraftCell::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::leftKey)  { grid.focusNeighbour (slot, -1, 0); return true; }
    if (code == juce::KeyPress::rightKey) { grid.focusNeighbour (slot,  1, 0); return true; }
    if (code == juce::KeyPress::upKey)    { grid.focusNeighbour (slot, 0, -1); return true; }
    if (code == juce::KeyPress::downKey)  { grid.focusNeighbour (slot, 0,  1); return true; }

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
        cell->setTooltip ("drop a block");
        addAndMakeVisible (*cell);
        cells.push_back (std::move (cell));
    }
    auto baseCell = std::make_unique<CraftCell> (*this, -1, true);
    baseCell->setTooltip (baseTooltip (grid.base));
    addAndMakeVisible (*baseCell);
    cells.push_back (std::move (baseCell));

    setSize (kCraftGridW, kCraftGridW);
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
    flashSlot (slot);
    cells[static_cast<size_t> (slot)]->repaint();
    notifyEdit();
}

void CraftGridComponent::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumCells || grid.cells[slot] == Material::none)
        return;
    grid.cells[slot] = Material::none;
    flashSlot (slot);
    cells[static_cast<size_t> (slot)]->repaint();
    notifyEdit();
}

void CraftGridComponent::moveMaterial (int fromSlot, int toSlot)
{
    if (fromSlot == toSlot || fromSlot < 0 || fromSlot >= kNumCells
        || toSlot < 0 || toSlot >= kNumCells)
        return;
    std::swap (grid.cells[fromSlot], grid.cells[toSlot]);   // swap keeps blocks
    flashSlot (toSlot);
    flashSlot (fromSlot);
    cells[static_cast<size_t> (fromSlot)]->repaint();
    cells[static_cast<size_t> (toSlot)]->repaint();
    notifyEdit();
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
