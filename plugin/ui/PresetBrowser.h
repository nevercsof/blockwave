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

// Preset browser overlay, Arturia-style two-pane folder navigation:
//
//   left pane  — folder tree: FAVORITES, ALL, then the FACTORY and USER banks, each
//                holding its categories (LEAD, BASS, ... "PAD (16)") as
//                pixel folder icons with preset counts;
//   right pane — the presets of the selected folder. ALL / bank folders
//                keep the category headers; a category folder is flat.
//
// Click loads (message thread -> atomic parameter writes), current preset
// highlighted, each row shows its craft recipe as a 12x12 mini 3x3 icon
// (SPEC §UI: users learn crafting by inspecting factory sounds).
// Keyboard: up/down walks the focused pane (folders filter immediately),
// tab / right into the list, left back to the folders, enter loads,
// F stars/unstars the focused row, escape closes. Wheel scrolls the pane
// under the mouse.
//
// SEARCH (producer request: "кнопочку с поиском пресетов добавь") — a field
// across the top of the panel, above both panes:
//
//   - filters live on every keystroke, case-insensitive substring of the
//     preset NAME (not the category, not the author);
//   - searches the WHOLE bank, both banks, ignoring the selected folder, and
//     groups the hits under the usual category headers;
//   - shows the match count in the field ("12 HITS" / "NO HITS");
//   - clears from the X in the field, or with ESC. ESC on an EMPTY field
//     still closes the browser, exactly as it did before the field existed.
//
// SEARCH vs FOLDER — last one touched wins, in both directions and with no
// third state to reason about: typing overrides the folder (the list goes
// bank-wide and the folder tree greys out), and touching a folder at all —
// clicking one, arrowing through the tree, or selectFolder() from a tool —
// clears the query and hands the list straight back to that folder.
//
// FOCUS — the browser owns the keyboard itself; there is no child text
// editor, so "focus" is a zone (Zone below), and only ONE zone at a time
// consumes plain letters. That is what keeps the field from eating the
// existing bindings — F still stars the highlighted row, because F only
// types while the SEARCH zone is active.
//
//   TAB / SHIFT+TAB   cycle SEARCH -> FOLDERS -> LIST -> SEARCH.
//   CTRL/CMD+F        jump straight to SEARCH from anywhere (not a bare
//                     letter, so it can never collide with typing).
//   click             the field, a folder or a row moves the zone there.
//   in SEARCH         letters/digits/space/-/_ type, BACKSPACE deletes,
//                     ESC clears (then closes), UP/DOWN/RETURN drop into the
//                     result list so you can type-then-arrow without TAB.
//   in FOLDERS/LIST   every key does exactly what it did before the field
//                     existed, F included.
//
// The browser always opens on the FOLDERS zone with an empty query.
//
// FAVORITES (producer request): every row carries an 8x8 star toggle at its
// right edge — clicking the star area toggles and does NOT load the preset
// (the star is hit-tested before the row). The FAVORITES folder sits at the
// top of the tree with a live count and lists every starred preset across
// both banks, grouped by category; empty, it shows a pixel hint.
//
// SavePanel: minimal pixel-art save dialog — name entry drawn with the
// bitmap font (own key handling, no native text editor), category cycler,
// SAVE / CANCEL.

#include "../PluginProcessor.h"
#include "ParamCells.h"
#include "MaterialArt.h"

namespace blockwave::ui
{

class PresetBrowser final : public juce::Component
{
public:
    explicit PresetBrowser (BlockwaveAudioProcessor& processor);

    std::function<void (int)> onLoad;                 // library index
    std::function<void()> onClose;
    std::function<void()> onFavoritesChanged;         // top-bar star follows

    void refresh();                                   // rebuild folders + rows

    // Bank id of the FAVORITES pseudo-folder (not a real preset bank).
    static constexpr int kFavoritesBank = 2;

    // Select a folder programmatically (also used by tools/screenshots).
    // bank: -1 = ALL, 0 = FACTORY, 1 = USER, kFavoritesBank = FAVORITES;
    // category empty = whole bank. Clears any active search, like every other
    // way of touching a folder.
    // Returns false when no such folder exists (e.g. empty category).
    bool selectFolder (int bank, const juce::String& category);

    // ---- search (also the tools/screenshots + component-test seam) --------
    static constexpr int kMaxQueryLength = 24;
    void setSearchQuery (const juce::String& text);   // uppercased, clamped
    void clearSearch();
    const juce::String& getSearchQuery() const noexcept { return query; }
    bool isSearching() const noexcept { return query.isNotEmpty(); }
    int getSearchMatchCount() const noexcept { return presetsListed; }
    void focusSearchField();                          // CTRL/CMD+F, field click

    // Scroll offset of the preset pane, in pixels. Read-only, and read by the
    // component tests: it is the observable that proves a wheel notch aimed at
    // the list actually REACHED the list, which is the property the editor's
    // settle shield must not break while it is armed over the top bar.
    int getListScrollY() const noexcept { return scrollY; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    // Which zone of the browser the keyboard is talking to. Only the SEARCH
    // zone consumes plain letters, which is what keeps F (star) alive.
    enum class Zone { search, folders, list };

    struct Folder
    {
        juce::String label;                           // "ALL", "FACTORY", "PAD"...
        int bank = -1;                                // -1 all, 0 factory, 1 user
        int catRank = -1;                             // -1 all, else categoryRank
        int indent = 0;                               // 0 bank-level, 1 category
        int count = 0;                                // presets inside
    };

    struct Row
    {
        bool header = false;
        juce::String text;
        int presetIndex = -1;
        bool isFactory = false;
        bool hasCraft = false;                        // parsed once in rebuild
        CraftGrid craft;
    };

    juce::Rectangle<int> panelRect() const;
    juce::Rectangle<int> searchRect() const;          // field, above the panes
    juce::Rectangle<int> searchClearRect() const;     // X inside the field
    juce::Rectangle<int> folderRect() const;          // outer bevel box, left
    juce::Rectangle<int> listRect() const;            // outer bevel box, right
    const Folder& selectedFolder() const;
    bool typeIntoSearch (const juce::KeyPress&);      // SEARCH-zone key path
    void rebuildFolders();
    void rebuildRows();                               // from the selected folder
    int rowAt (juce::Point<int> pos) const;
    int folderAt (juce::Point<int> pos) const;
    juce::Rectangle<int> starRectForRow (int rowIndex) const;   // canvas coords
    void toggleFavoriteRow (int rowIndex);
    void setFolderCursor (int index);
    void moveCursor (int delta);
    void moveFolderCursor (int delta);
    void loadRow (int rowIndex);
    void keepCursorVisible();

    BlockwaveAudioProcessor& proc;
    juce::TextButton closeBtn { "X" };
    std::vector<Folder> folders;
    std::vector<Row> rows;
    int folderCursor = 0;                             // selected folder
    int cursor = 0;                                   // keyboard cursor (row)
    Zone zone = Zone::folders;                        // key focus zone
    juce::String query;                               // search, "" = inactive
    int scrollY = 0, folderScrollY = 0;
    int presetsListed = 0;                            // non-header rows

    static constexpr int kRowH = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

class SavePanel final : public juce::Component
{
public:
    SavePanel();

    std::function<void (const juce::String& name, const juce::String& category)> onSave;
    std::function<void()> onCancel;

    void open (const juce::String& currentName, const juce::String& currentCategory);
    void setError (const juce::String&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    juce::Rectangle<int> panelRect() const;
    void cycleCategory (int delta);
    void doSave();

    juce::TextButton saveBtn { "SAVE" }, cancelBtn { "CANCEL" },
                     catPrev { "<" }, catNext { ">" };
    juce::String name, error;
    int catIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SavePanel)
};

} // namespace blockwave::ui
