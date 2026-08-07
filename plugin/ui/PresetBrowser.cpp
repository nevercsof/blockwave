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

    // 7x7 magnifier, original pixel art: 5x5 ring plus a 2x2 handle. Marks
    // the search field without a word of chrome.
    void drawMagnifier (juce::Graphics& g, int x, int y, juce::Colour c)
    {
        g.setColour (c);
        g.fillRect (x + 1, y,     3, 1);
        g.fillRect (x,     y + 1, 1, 3);
        g.fillRect (x + 4, y + 1, 1, 3);
        g.fillRect (x + 1, y + 4, 3, 1);
        g.fillRect (x + 4, y + 4, 2, 2);
        g.fillRect (x + 5, y + 5, 2, 2);
    }

    // 6x6 X glyph for the clear button.
    void drawClearGlyph (juce::Graphics& g, int x, int y, juce::Colour c)
    {
        g.setColour (c);
        for (int i = 0; i < 6; ++i)
        {
            g.fillRect (x + i, y + i, 1, 1);
            g.fillRect (x + 5 - i, y + i, 1, 1);
        }
    }

    const juce::String kQueryChars ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
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

// The search field takes a 24 px band off the top of both panes: the list
// drops from 23 visible rows to 21 on the fixed 832x456 canvas, which is the
// whole cost of the feature (flagged in the checkpoint, not squeezed).
juce::Rectangle<int> PresetBrowser::searchRect() const
{
    auto r = panelRect();
    return { r.getX() + 8, r.getY() + 24, r.getWidth() - 16, 20 };
}

juce::Rectangle<int> PresetBrowser::searchClearRect() const
{
    const auto s = searchRect();
    return { s.getRight() - 18, s.getY() + 2, 16, 16 };
}

juce::Rectangle<int> PresetBrowser::folderRect() const
{
    auto r = panelRect();
    return { r.getX() + 8, r.getY() + 48, 160, r.getHeight() - 56 };
}

juce::Rectangle<int> PresetBrowser::listRect() const
{
    auto r = panelRect();
    return { r.getX() + 176, r.getY() + 48, r.getWidth() - 184,
             r.getHeight() - 56 };
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

    // FAVORITES sits above ALL and is always present — at (0) it doubles as
    // the "you can star things" affordance.
    folders.push_back ({ "FAVORITES", kFavoritesBank, -1, 0, lib.getNumFavorites() });
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
    const bool searching = isSearching();
    // A search is bank-wide by definition, so it keeps the category headers
    // whatever folder happens to be selected underneath it.
    const bool grouped = searching || f.catRank < 0;

    juce::String lastCat ("\x01");
    for (int i = 0; i < lib.getNumPresets(); ++i)
    {
        const auto& e = lib.getPreset (i);
        if (searching)
        {
            // Name substring only, case-insensitive. The folder filter is
            // deliberately ignored: typing searches everything you own.
            if (! e.name.containsIgnoreCase (query))
                continue;
        }
        else if (f.bank == kFavoritesBank)
        {
            if (! e.isFavorite)                      // starred, any bank
                continue;
        }
        else if (f.bank >= 0 && e.isFactory != (f.bank == 0))
            continue;
        if (! searching && f.catRank >= 0 && categoryRank (e.category) != f.catRank)
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
            query.clear();                   // touching a folder ends a search
            folderCursor = static_cast<int> (i);
            zone = Zone::folders;
            rebuildRows();
            return true;
        }
    return false;
}

// ---- search ------------------------------------------------------------------

void PresetBrowser::setSearchQuery (const juce::String& text)
{
    const auto next = text.toUpperCase().substring (0, kMaxQueryLength);
    if (next == query)
        return;
    query = next;
    rebuildRows();                           // repaints; live on every keystroke
}

void PresetBrowser::clearSearch()
{
    if (query.isEmpty())
        return;
    query.clear();
    rebuildRows();
}

void PresetBrowser::focusSearchField()
{
    if (zone == Zone::search)
        return;
    zone = Zone::search;
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
    {
        // While a search is running the title-bar readout says WHAT is being
        // filtered instead of repeating the hit count shown in the field.
        const juce::String head = isSearching()
            ? "SEARCH: " + query
            : juce::String (presetsListed)
                  + (presetsListed == 1 ? " SOUND" : " SOUNDS");
        drawPixelText (g, head,
                       p.getRight() - 28 - pixelTextWidth (head, 1),
                       p.getY() + 8, 1, isSearching() ? lava : dimText);
    }

    // ---- search field ------------------------------------------------------
    {
        const auto s = searchRect();
        const bool active = zone == Zone::search;
        drawBevelBox (g, s, chip, panelDark, panelLight, outline, true);
        if (isSearching() || active)
        {
            g.setColour (isSearching() ? lava : ice);   // live filter marker
            g.fillRect (s.getX() + 1, s.getY() + 1, 2, s.getHeight() - 2);
        }
        drawMagnifier (g, s.getX() + 7, s.getY() + 6,
                       isSearching() ? lava : dimText);

        const int textX = s.getX() + 20;
        if (query.isEmpty())
        {
            drawPixelText (g, active ? "TYPE TO FILTER" : "SEARCH PRESETS",
                           textX, s.getY() + 7, 1, dimText);
        }
        else
        {
            const int tw = drawPixelText (g, query, textX, s.getY() + 7, 1, ice);
            if (active)
            {
                g.setColour (lava);                      // solid block caret
                g.fillRect (textX + tw + 1, s.getY() + 6, 4, 7);
            }
        }

        // Hit count, then the clear button (only when there is something to
        // clear). Both sit inside the field's right edge.
        const bool none = isSearching() && presetsListed == 0;
        if (isSearching())
        {
            const juce::String hits = none ? juce::String ("NO HITS")
                : juce::String (presetsListed)
                      + (presetsListed == 1 ? " HIT" : " HITS");
            drawPixelText (g, hits,
                           searchClearRect().getX() - 6 - pixelTextWidth (hits, 1),
                           s.getY() + 7, 1, none ? lava : grass);

            const auto x = searchClearRect();
            drawBevelBox (g, x, buttonFace, panelLight, panelDark, outline);
            drawClearGlyph (g, x.getCentreX() - 3, x.getCentreY() - 3, buttonText);
        }
        else if (! active)
        {
            drawPixelText (g, "CTRL+F", s.getRight() - 8 - pixelTextWidth ("CTRL+F", 1),
                           s.getY() + 7, 1, panelLight);
        }
    }

    // ---- left pane: folder tree -------------------------------------------
    // Greyed out while a search is running: the list is bank-wide and the
    // folder selection is not what you are looking at.
    const bool foldersLive = ! isSearching();
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
                g.setColour (foldersLive ? dirt : titleBar);
                g.fillRect (fInner.getX(), y, fInner.getWidth(), kRowH);
                g.setColour (! foldersLive ? panelDark
                             : zone == Zone::folders ? ice : stone);
                g.drawRect (fInner.getX(), y, fInner.getWidth(), kRowH, 1);
            }

            const int ix = fInner.getX() + 4 + f.indent * 12;
            const bool isFavFolder = f.bank == kFavoritesBank;
            if (isFavFolder)
            {
                // A star, not a folder: it is a view, not a bank on disk.
                drawPixelStar (g, ix + 1, y + 4, f.count > 0, colours::starGold, stone);
            }
            else
            {
                const auto body = f.bank < 0 ? grass
                                : f.indent == 0 ? stone : dirt.brighter (0.25f);
                drawFolderIcon (g, ix, y + 4, body);
            }

            const auto textCol = ! foldersLive ? panelFace
                               : sel ? label
                               : isFavFolder ? colours::starGold
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
            g.setColour (zone == Zone::list ? ice : stone);   // active zone = ice
            g.drawRect (list.getX(), y, list.getWidth(), kRowH, 1);
        }

        // Mini 3x3 craft icon: the preset's recipe at a glance.
        if (row.hasCraft)
            drawMiniCraftIcon (g, row.craft, list.getX() + 8, y + 2);
        else
            drawMiniCraftIconEmpty (g, list.getX() + 8, y + 2);

        drawPixelText (g, row.text, list.getX() + 28, y + 5, 1,
                       isCurrent ? label : dimText);

        // Star toggle: its own click target (see starRectForRow / mouseDown).
        drawPixelStar (g, list.getRight() - 30, y + 4,
                  proc.getPresetLibrary().isFavorite (row.presetIndex),
                  colours::starGold, panelFace);

        drawPixelText (g, row.isFactory ? "F" : "U",
                       list.getRight() - 12, y + 5, 1,
                       row.isFactory ? stone : lava);
    }

    // Empty states: the pane never just sits there blank.
    if (presetsListed == 0 && isSearching())
    {
        drawMagnifier (g, list.getCentreX() - 3, list.getY() + 40, panelFace);
        const juce::String hint ("NO SOUND BY THAT NAME");
        drawPixelText (g, hint, list.getCentreX() - pixelTextWidth (hint, 1) / 2,
                       list.getY() + 56, 1, dimText);
    }
    else if (presetsListed == 0 && selectedFolder().bank == kFavoritesBank)
    {
        drawPixelStar (g, list.getCentreX() - 4, list.getY() + 40, false,
                  colours::starGold, panelFace);
        const juce::String hint ("STAR A PRESET TO PIN IT");
        drawPixelText (g, hint, list.getCentreX() - pixelTextWidth (hint, 1) / 2,
                       list.getY() + 56, 1, dimText);
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

// The star's click target: 16x16 around the 8x8 art, so it is comfortable
// at 1x without eating the preset name.
juce::Rectangle<int> PresetBrowser::starRectForRow (int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= static_cast<int> (rows.size())
        || rows[static_cast<size_t> (rowIndex)].header)
        return {};
    const auto list = listRect().reduced (3);
    const int y = list.getY() - scrollY + rowIndex * kRowH;
    return { list.getRight() - 34, y, 16, kRowH };
}

void PresetBrowser::toggleFavoriteRow (int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= static_cast<int> (rows.size()))
        return;
    const auto& row = rows[static_cast<size_t> (rowIndex)];
    if (row.header || row.presetIndex < 0)
        return;

    proc.getPresetLibrary().toggleFavorite (row.presetIndex);

    // The FAVORITES count (and, inside that folder, the list itself) changed.
    const auto keepBank = selectedFolder().bank;
    const auto keepRank = selectedFolder().catRank;
    const int keepScroll = scrollY;
    rebuildFolders();
    for (size_t i = 0; i < folders.size(); ++i)
        if (folders[i].bank == keepBank && folders[i].catRank == keepRank)
            folderCursor = static_cast<int> (i);
    if (keepBank == kFavoritesBank)
    {
        rebuildRows();                               // the row may have left
    }
    else
    {
        scrollY = keepScroll;                        // stay exactly where we are
        repaint();
    }

    if (onFavoritesChanged)
        onFavoritesChanged();
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
    // Touching a folder in ANY way ends the search — click, arrow key or
    // tool call. Typing overrides the folder, a folder overrides typing;
    // there is never a third state where both are half applied.
    query.clear();
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

    if (searchRect().contains (e.getPosition()))
    {
        if (isSearching() && searchClearRect().contains (e.getPosition()))
            clearSearch();
        focusSearchField();
        repaint();
        return;
    }

    const int f = folderAt (e.getPosition());
    if (f >= 0)
    {
        zone = Zone::folders;
        setFolderCursor (f);                     // also clears the search
        return;
    }

    const int r = rowAt (e.getPosition());
    if (r >= 0 && ! rows[static_cast<size_t> (r)].header)
    {
        zone = Zone::list;
        cursor = r;
        // Star first: clicking the star ONLY toggles, it never loads.
        if (starRectForRow (r).contains (e.getPosition()))
        {
            toggleFavoriteRow (r);
            return;
        }
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

// SEARCH zone only: plain letters type here and nowhere else, which is what
// leaves F free to star a row in the list.
bool PresetBrowser::typeIntoSearch (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::backspaceKey)
        || k.isKeyCode (juce::KeyPress::deleteKey))
    {
        setSearchQuery (query.dropLastCharacters (1));
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::upKey)
        || k.isKeyCode (juce::KeyPress::returnKey))
    {
        zone = Zone::list;                           // type, then arrow in
        repaint();
        return true;
    }
    const auto c = static_cast<juce::juce_wchar> (
        juce::CharacterFunctions::toUpperCase (k.getTextCharacter()));
    if (kQueryChars.containsChar (c) && query.length() < kMaxQueryLength)
    {
        setSearchQuery (query + juce::String::charToString (c));
        return true;
    }
    return false;
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();

    if (k.isKeyCode (juce::KeyPress::escapeKey))
    {
        // ESC clears a live search; on an empty field it closes the browser,
        // exactly as it did before the field existed.
        if (isSearching())
        {
            clearSearch();
            return true;
        }
        if (onClose)
            onClose();
        return true;
    }
    // CTRL/CMD+F: jump to the field from any zone. Not a bare letter, so it
    // can never be swallowed by typing (and F still stars in the list).
    if ((mods.isCommandDown() || mods.isCtrlDown()) && k.getKeyCode() == 'F')
    {
        focusSearchField();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::tabKey))
    {
        // SEARCH -> FOLDERS -> LIST -> SEARCH (shift reverses).
        static constexpr Zone fwd[3] = { Zone::folders, Zone::list, Zone::search };
        static constexpr Zone back[3] = { Zone::list, Zone::search, Zone::folders };
        const int i = static_cast<int> (zone);
        zone = mods.isShiftDown() ? back[i] : fwd[i];
        repaint();
        return true;
    }

    if (zone == Zone::search)                        // ---- search field -----
        return typeIntoSearch (k);

    if (zone == Zone::folders)                       // ---- folder pane ------
    {
        if (k.isKeyCode (juce::KeyPress::upKey))   { moveFolderCursor (-1); return true; }
        if (k.isKeyCode (juce::KeyPress::downKey)) { moveFolderCursor (1);  return true; }
        if (k.isKeyCode (juce::KeyPress::rightKey)
            || k.isKeyCode (juce::KeyPress::returnKey))
        {
            zone = Zone::list;
            repaint();
            return true;
        }
        return false;
    }

    // ---- list pane --------------------------------------------------------
    if (k.isKeyCode (juce::KeyPress::leftKey))
    {
        zone = Zone::folders;
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
    if (juce::CharacterFunctions::toUpperCase (k.getTextCharacter()) == 'F')
    {
        toggleFavoriteRow (cursor);                  // star the focused row
        return true;
    }
    return false;
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        zone = Zone::folders;                        // open on the folder pane
        query.clear();                               // ...with a clean field
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
