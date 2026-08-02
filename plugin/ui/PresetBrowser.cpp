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
    return { (getWidth() - 512) / 2, 8, 512, getHeight() - 16 };
}

juce::Rectangle<int> PresetBrowser::listRect() const
{
    auto r = panelRect();
    return { r.getX() + 8, r.getY() + 24, r.getWidth() - 16, r.getHeight() - 32 };
}

void PresetBrowser::refresh()
{
    rows.clear();
    const auto& lib = proc.getPresetLibrary();
    juce::String lastCat ("\x01");
    for (int i = 0; i < lib.getNumPresets(); ++i)
    {
        const auto& e = lib.getPreset (i);
        auto cat = e.category.toUpperCase();
        if (cat.isEmpty())
            cat = "OTHER";
        if (cat != lastCat)
        {
            rows.push_back ({ true, cat, -1, false, false, {} });
            lastCat = cat;
        }

        // Mini recipe icon data: parse the preset's craft object once here,
        // never in paint().
        Row row { false, e.name.toUpperCase(), i, e.isFactory, false, {} };
        row.hasCraft = craftGridFromVar (e.root.getProperty ("craft", juce::var()),
                                         row.craft);
        rows.push_back (row);
    }

    cursor = 0;
    for (size_t r = 0; r < rows.size(); ++r)
        if (rows[r].presetIndex == lib.getCurrentIndex())
            cursor = static_cast<int> (r);
    repaint();
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
    drawPixelText (g, juce::String (proc.getPresetLibrary().getNumPresets())
                          + " SOUNDS",
                   p.getX() + p.getWidth() - 96, p.getY() + 8, 1, dimText);

    const auto list = listRect();
    g.setColour (chip);
    g.fillRect (list);

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
            g.setColour (ice);
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
    const auto list = listRect();
    if (! list.contains (pos))
        return -1;
    const int r = (pos.y - list.getY() + scrollY) / kRowH;
    return r >= 0 && r < static_cast<int> (rows.size()) ? r : -1;
}

void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (! panelRect().contains (e.getPosition()))
    {
        if (onClose)
            onClose();
        return;
    }
    const int r = rowAt (e.getPosition());
    if (r >= 0 && ! rows[static_cast<size_t> (r)].header)
    {
        cursor = r;
        loadRow (r);
    }
}

void PresetBrowser::mouseWheelMove (const juce::MouseEvent&,
                                    const juce::MouseWheelDetails& w)
{
    const int total = static_cast<int> (rows.size()) * kRowH;
    const int maxScroll = juce::jmax (0, total - listRect().getHeight());
    scrollY = juce::jlimit (0, maxScroll,
                            scrollY - static_cast<int> (w.deltaY * 48.0f));
    repaint();
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

    // Keep the cursor visible.
    const auto list = listRect();
    const int rowTop = cursor * kRowH;
    if (rowTop < scrollY)
        scrollY = rowTop;
    else if (rowTop + kRowH > scrollY + list.getHeight())
        scrollY = rowTop + kRowH - list.getHeight();
    repaint();
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
