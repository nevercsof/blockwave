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

#include "PresetBrowser.h"

namespace blockwave::ui
{

// ---- PresetBrowser ----------------------------------------------------------

namespace
{
    // Canonical category display names, index == blockwave::categoryRank.
    const char* const kCatNames[9] = { "LEAD", "BASS", "PLUCK", "PAD", "KEYS",
                                       "CHIP", "PERC", "FX", "OTHER" };

    // 10x8 chunky pixel folder icon (original art, pure fillRects).
    void drawFolderIcon (juce::Graphics& g, int x, int y, juce::Colour body)
    {
        g.setColour (colours::outline);
        g.fillRect (x, y, 5, 2);                     // tab
        g.fillRect (x, y + 1, 10, 7);                // body outline
        g.setColour (body);
        g.fillRect (x + 1, y + 1, 3, 1);             // tab face
        g.fillRect (x + 1, y + 2, 8, 5);             // body face
        g.setColour (body.brighter (0.35f));
        g.fillRect (x + 1, y + 2, 8, 1);             // lid highlight
    }
}

PresetBrowser::PresetBrowser (BlockwaveAudioProcessor& processor)
    : proc (processor)
{
    setWantsKeyboardFocus (true);
    closeBtn.setTooltip ("close browser");
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);
    refresh();
}

juce::Rectangle<int> PresetBrowser::panelRect() const
{
    return { (getWidth() - 640) / 2, 8, 640, getHeight() - 16 };
}

juce::Rectangle<int> PresetBrowser::folderRect() const
{
    auto r = panelRect();
    return { r.getX() + 8, r.getY() + 24, 160, r.getHeight() - 32 };
}

juce::Rectangle<int> PresetBrowser::listRect() const
{
    auto r = panelRect();
    return { r.getX() + 176, r.getY() + 24, r.getWidth() - 184,
             r.getHeight() - 32 };
}

const PresetBrowser::Folder& PresetBrowser::selectedFolder() const
{
    static const Folder all;                         // safe fallback: ALL
    if (folders.empty())
        return all;
    const auto i = juce::jlimit (0, static_cast<int> (folders.size()) - 1,
                                 folderCursor);
    return folders[static_cast<size_t> (i)];
}

void PresetBrowser::rebuildFolders()
{
    folders.clear();
    const auto& lib = proc.getPresetLibrary();

    folders.push_back ({ "ALL", -1, -1, 0, lib.getNumPresets() });

    for (int bank = 0; bank < 2; ++bank)             // 0 factory, 1 user
    {
        const bool wantFactory = bank == 0;
        int counts[9] = {};
        int bankCount = 0;
        for (int i = 0; i < lib.getNumPresets(); ++i)
        {
            const auto& e = lib.getPreset (i);
            if (e.isFactory != wantFactory)
                continue;
            ++bankCount;
            ++counts[categoryRank (e.category)];
        }

        folders.push_back ({ wantFactory ? "FACTORY" : "USER",
                             bank, -1, 0, bankCount });
        for (int r = 0; r < 9; ++r)
            if (counts[r] > 0)
                folders.push_back ({ kCatNames[r], bank, r, 1, counts[r] });
    }
}

void PresetBrowser::rebuildRows()
{
    rows.clear();
    presetsListed = 0;
    const auto& lib = proc.getPresetLibrary();
    const auto& f = selectedFolder();
    const bool grouped = f.catRank < 0;              // ALL / bank keep headers

    juce::String lastCat ("\x01");
    for (int i = 0; i < lib.getNumPresets(); ++i)
    {
        const auto& e = lib.getPreset (i);
        if (f.bank >= 0 && e.isFactory != (f.bank == 0))
            continue;
        if (f.catRank >= 0 && categoryRank (e.category) != f.catRank)
            continue;

        if (grouped)
        {
            const juce::String cat (kCatNames[categoryRank (e.category)]);
            if (cat != lastCat)
            {
                rows.push_back ({ true, cat, -1, false, false, {} });
                lastCat = cat;
            }
        }

        // Mini recipe icon data: parse the preset's craft object once here,
        // never in paint().
        Row row { false, e.name.toUpperCase(), i, e.isFactory, false, {} };
        row.hasCraft = craftGridFromVar (e.root.getProperty ("craft", juce::var()),
                                         row.craft);
        rows.push_back (row);
        ++presetsListed;
    }

    // Cursor: the current preset if it is in this folder, else first preset.
    cursor = 0;
    scrollY = 0;
    for (size_t r = 0; r < rows.size(); ++r)
        if (! rows[r].header)
        {
            cursor = static_cast<int> (r);
            break;
        }
    for (size_t r = 0; r < rows.size(); ++r)
        if (rows[r].presetIndex == lib.getCurrentIndex())
            cursor = static_cast<int> (r);
    keepCursorVisible();
    repaint();
}

void PresetBrowser::refresh()
{
    // Keep the selected folder across refreshes (preset saved, bank rescan).
    const auto keepBank = selectedFolder().bank;
    const auto keepRank = selectedFolder().catRank;

    rebuildFolders();
    folderCursor = 0;
    for (size_t i = 0; i < folders.size(); ++i)
        if (folders[i].bank == keepBank && folders[i].catRank == keepRank)
            folderCursor = static_cast<int> (i);
    rebuildRows();
}

bool PresetBrowser::selectFolder (int bank, const juce::String& category)
{
    const int rank = category.isEmpty() ? -1 : categoryRank (category);
    for (size_t i = 0; i < folders.size(); ++i)
        if (folders[i].bank == bank && folders[i].catRank == rank)
        {
            folderCursor = static_cast<int> (i);
            focusList = false;
            rebuildRows();
            return true;
        }
    return false;
}

void PresetBrowser::resized()
{
    const auto p = panelRect();
    closeBtn.setBounds (p.getRight() - 24, p.getY() + 4, 16, 16);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    using namespace colours;
    g.fillAll (night.withAlpha (0.78f));                 // dim the synth

    const auto p = panelRect();
    drawBevelBox (g, p, panelFace, panelLight, panelDark, outline);
    g.setColour (titleBar);
    g.fillRect (p.getX() + 3, p.getY() + 3, p.getWidth() - 6, 18);
    drawPixelText (g, "PRESETS", p.getX() + 8, p.getY() + 8, 1, label);
    drawPixelText (g, juce::String (presetsListed)
                          + (presetsListed == 1 ? " SOUND" : " SOUNDS"),
                   p.getX() + p.getWidth() - 104, p.getY() + 8, 1, dimText);

    // ---- left pane: folder tree -------------------------------------------
    const auto fBox = folderRect();
    drawBevelBox (g, fBox, chip, panelDark, panelLight, outline, true);
    const auto fInner = fBox.reduced (3);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (fInner);

        int y = fInner.getY() - folderScrollY;
        for (size_t i = 0; i < folders.size(); ++i, y += kRowH)
        {
            if (y + kRowH < fInner.getY() || y > fInner.getBottom())
                continue;
            const auto& f = folders[i];
            const bool sel = static_cast<int> (i) == folderCursor;
            if (sel)
            {
                g.setColour (dirt);
                g.fillRect (fInner.getX(), y, fInner.getWidth(), kRowH);
                g.setColour (focusList ? stone : ice);   // active pane = ice
                g.drawRect (fInner.getX(), y, fInner.getWidth(), kRowH, 1);
            }

            const int ix = fInner.getX() + 4 + f.indent * 12;
            const auto body = f.bank < 0 ? grass
                            : f.indent == 0 ? stone : dirt.brighter (0.25f);
            drawFolderIcon (g, ix, y + 4, body);

            const auto textCol = sel ? label
                               : f.indent == 0 ? grass : dimText;
            drawPixelText (g, f.label, ix + 14, y + 5, 1, textCol);

            const juce::String count ("(" + juce::String (f.count) + ")");
            drawPixelText (g, count,
                           fInner.getRight() - 4 - pixelTextWidth (count, 1),
                           y + 5, 1, sel ? label : panelLight);
        }
    }

    // ---- right pane: preset list ------------------------------------------
    const auto lBox = listRect();
    drawBevelBox (g, lBox, chip, panelDark, panelLight, outline, true);
    const auto list = lBox.reduced (3);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (list);

    const int current = proc.getPresetLibrary().getCurrentIndex();
    int y = list.getY() - scrollY;
    for (size_t r = 0; r < rows.size(); ++r, y += kRowH)
    {
        if (y + kRowH < list.getY() || y > list.getBottom())
            continue;
        const auto& row = rows[r];
        if (row.header)
        {
            g.setColour (dirt);
            g.fillRect (list.getX(), y + kRowH - 3, list.getWidth(), 1);
            drawPixelText (g, row.text, list.getX() + 4, y + 5, 1, grass);
            continue;
        }

        const bool isCurrent = row.presetIndex == current;
        if (isCurrent)
        {
            g.setColour (dirt);
            g.fillRect (list.getX(), y, list.getWidth(), kRowH);
        }
        if (static_cast<int> (r) == cursor)
        {
            g.setColour (focusList ? ice : stone);       // active pane = ice
            g.drawRect (list.getX(), y, list.getWidth(), kRowH, 1);
        }

        // Mini 3x3 craft icon: the preset's recipe at a glance.
        if (row.hasCraft)
            drawMiniCraftIcon (g, row.craft, list.getX() + 8, y + 2);
        else
            drawMiniCraftIconEmpty (g, list.getX() + 8, y + 2);

        drawPixelText (g, row.text, list.getX() + 28, y + 5, 1,
                       isCurrent ? label : dimText);
        drawPixelText (g, row.isFactory ? "F" : "U",
                       list.getRight() - 12, y + 5, 1,
                       row.isFactory ? stone : lava);
    }
}

int PresetBrowser::rowAt (juce::Point<int> pos) const
{
    const auto list = listRect().reduced (3);
    if (! list.contains (pos))
        return -1;
    const int r = (pos.y - list.getY() + scrollY) / kRowH;
    return r >= 0 && r < static_cast<int> (rows.size()) ? r : -1;
}

int PresetBrowser::folderAt (juce::Point<int> pos) const
{
    const auto inner = folderRect().reduced (3);
    if (! inner.contains (pos))
        return -1;
    const int f = (pos.y - inner.getY() + folderScrollY) / kRowH;
    return f >= 0 && f < static_cast<int> (folders.size()) ? f : -1;
}

void PresetBrowser::setFolderCursor (int index)
{
    folderCursor = juce::jlimit (0, juce::jmax (0, static_cast<int> (folders.size()) - 1),
                                 index);

    // Keep the selected folder visible.
    const auto inner = folderRect().reduced (3);
    const int top = folderCursor * kRowH;
    if (top < folderScrollY)
        folderScrollY = top;
    else if (top + kRowH > folderScrollY + inner.getHeight())
        folderScrollY = top + kRowH - inner.getHeight();

    rebuildRows();                                   // selection filters live
}

void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (! panelRect().contains (e.getPosition()))
    {
        if (onClose)
            onClose();
        return;
    }

    const int f = folderAt (e.getPosition());
    if (f >= 0)
    {
        focusList = false;
        setFolderCursor (f);
        return;
    }

    const int r = rowAt (e.getPosition());
    if (r >= 0 && ! rows[static_cast<size_t> (r)].header)
    {
        focusList = true;
        cursor = r;
        loadRow (r);
    }
}

void PresetBrowser::mouseWheelMove (const juce::MouseEvent& e,
                                    const juce::MouseWheelDetails& w)
{
    const int step = static_cast<int> (w.deltaY * 48.0f);
    if (folderRect().contains (e.getPosition()))
    {
        const int total = static_cast<int> (folders.size()) * kRowH;
        const int maxScroll =
            juce::jmax (0, total - folderRect().reduced (3).getHeight());
        folderScrollY = juce::jlimit (0, maxScroll, folderScrollY - step);
    }
    else
    {
        const int total = static_cast<int> (rows.size()) * kRowH;
        const int maxScroll =
            juce::jmax (0, total - listRect().reduced (3).getHeight());
        scrollY = juce::jlimit (0, maxScroll, scrollY - step);
    }
    repaint();
}

void PresetBrowser::keepCursorVisible()
{
    const auto list = listRect().reduced (3);
    const int rowTop = cursor * kRowH;
    if (rowTop < scrollY)
        scrollY = rowTop;
    else if (rowTop + kRowH > scrollY + list.getHeight())
        scrollY = rowTop + kRowH - list.getHeight();
}

void PresetBrowser::moveCursor (int delta)
{
    const int n = static_cast<int> (rows.size());
    if (n == 0)
        return;
    int r = cursor;
    do
    {
        r = juce::jlimit (0, n - 1, r + delta);
    } while (rows[static_cast<size_t> (r)].header
             && r + delta >= 0 && r + delta < n);
    if (! rows[static_cast<size_t> (r)].header)
        cursor = r;

    keepCursorVisible();
    repaint();
}

void PresetBrowser::moveFolderCursor (int delta)
{
    setFolderCursor (folderCursor + delta);
}

void PresetBrowser::loadRow (int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int> (rows.size()))
        return;
    const auto& row = rows[static_cast<size_t> (rowIndex)];
    if (! row.header && onLoad)
        onLoad (row.presetIndex);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (onClose)
            onClose();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::tabKey))
    {
        focusList = ! focusList;                     // hop between the panes
        repaint();
        return true;
    }

    if (! focusList)                                 // ---- folder pane ------
    {
        if (k.isKeyCode (juce::KeyPress::upKey))   { moveFolderCursor (-1); return true; }
        if (k.isKeyCode (juce::KeyPress::downKey)) { moveFolderCursor (1);  return true; }
        if (k.isKeyCode (juce::KeyPress::rightKey)
            || k.isKeyCode (juce::KeyPress::returnKey))
        {
            focusList = true;
            repaint();
            return true;
        }
        return false;
    }

    // ---- list pane --------------------------------------------------------
    if (k.isKeyCode (juce::KeyPress::leftKey))
    {
        focusList = false;
        repaint();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::upKey))   { moveCursor (-1); return true; }
    if (k.isKeyCode (juce::KeyPress::downKey)) { moveCursor (1);  return true; }
    if (k.isKeyCode (juce::KeyPress::returnKey))
    {
        loadRow (cursor);
        return true;
    }
    return false;
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        focusList = false;                           // open on the folder pane
        refresh();
        if (isShowing())
            grabKeyboardFocus();
    }
}

// ---- SavePanel --------------------------------------------------------------

SavePanel::SavePanel()
{
    setWantsKeyboardFocus (true);

    saveBtn.setTooltip ("write user preset");
    saveBtn.onClick = [this] { doSave(); };
    addAndMakeVisible (saveBtn);

    cancelBtn.setTooltip ("forget it");
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancelBtn);

    catPrev.setTooltip ("previous category");
    catPrev.onClick = [this] { cycleCategory (-1); };
    addAndMakeVisible (catPrev);

    catNext.setTooltip ("next category");
    catNext.onClick = [this] { cycleCategory (1); };
    addAndMakeVisible (catNext);
}

static const char* kCategories[] = { "LEAD", "BASS", "PLUCK", "PAD",
                                     "KEYS", "CHIP", "PERC", "FX" };

void SavePanel::open (const juce::String& currentName,
                      const juce::String& currentCategory)
{
    name = currentName.toUpperCase().substring (0, 18);
    error.clear();
    catIndex = juce::jlimit (0, 7, categoryRank (currentCategory));
    setVisible (true);
    toFront (true);
    if (isShowing())
        grabKeyboardFocus();
    repaint();
}

void SavePanel::setError (const juce::String& e)
{
    error = e.toUpperCase();
    repaint();
}

juce::Rectangle<int> SavePanel::panelRect() const
{
    return { (getWidth() - 320) / 2, (getHeight() - 176) / 2, 320, 176 };
}

void SavePanel::resized()
{
    const auto p = panelRect();
    catPrev.setBounds  (p.getX() + 88,  p.getY() + 96, 16, 16);
    catNext.setBounds  (p.getX() + 200, p.getY() + 96, 16, 16);
    saveBtn.setBounds  (p.getX() + 56,  p.getBottom() - 32, 96, 20);
    cancelBtn.setBounds (p.getX() + 168, p.getBottom() - 32, 96, 20);
}

void SavePanel::paint (juce::Graphics& g)
{
    using namespace colours;
    g.fillAll (night.withAlpha (0.78f));

    const auto p = panelRect();
    drawBevelBox (g, p, panelFace, panelLight, panelDark, outline);
    g.setColour (titleBar);
    g.fillRect (p.getX() + 3, p.getY() + 3, p.getWidth() - 6, 18);
    drawPixelText (g, "SAVE PRESET", p.getX() + 8, p.getY() + 8, 1, label);

    // Name field with a solid block caret (pixel art doesn't blink politely).
    drawPixelText (g, "NAME", p.getX() + 16, p.getY() + 32, 1, dimText);
    const juce::Rectangle<int> nameBox (p.getX() + 16, p.getY() + 42,
                                        p.getWidth() - 32, 20);
    drawBevelBox (g, nameBox, chip, panelLight, panelDark, outline, true);
    const int tw = drawPixelText (g, name, nameBox.getX() + 6,
                                  nameBox.getY() + 7, 1, ice);
    g.setColour (lava);
    g.fillRect (nameBox.getX() + 6 + tw + 1, nameBox.getY() + 6, 4, 8);

    drawPixelText (g, "CATEGORY", p.getX() + 16, p.getY() + 80, 1, dimText);
    const juce::Rectangle<int> catBox (p.getX() + 108, p.getY() + 96, 88, 16);
    drawBevelBox (g, catBox, chip, panelLight, panelDark, outline, true);
    drawPixelTextCentred (g, kCategories[catIndex], catBox, 1, ice);

    if (error.isNotEmpty())
        drawPixelText (g, error.substring (0, 36),
                       p.getX() + 16, p.getY() + 122, 1, lava);
}

void SavePanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panelRect().contains (e.getPosition()) && onCancel)
        onCancel();
}

void SavePanel::cycleCategory (int delta)
{
    catIndex = (catIndex + delta + 8) % 8;
    repaint();
}

void SavePanel::doSave()
{
    if (name.trim().isEmpty())
    {
        setError ("NAME THE SOUND FIRST");
        return;
    }
    if (onSave)
        onSave (name.trim(), kCategories[catIndex]);
}

bool SavePanel::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (onCancel)
            onCancel();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::returnKey))
    {
        doSave();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::backspaceKey))
    {
        name = name.dropLastCharacters (1);
        repaint();
        return true;
    }

    const auto c = static_cast<juce::juce_wchar> (
        juce::CharacterFunctions::toUpperCase (k.getTextCharacter()));
    const juce::String allowed ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
    if (allowed.containsChar (c) && name.length() < 18)
    {
        name += juce::String::charToString (c);
        repaint();
        return true;
    }
    return false;
}

void SavePanel::visibilityChanged()
{
    if (isVisible() && isShowing())
        grabKeyboardFocus();
}

} // namespace blockwave::ui
