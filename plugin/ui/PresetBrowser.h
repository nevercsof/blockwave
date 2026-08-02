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
//   left pane  — folder tree: ALL, then the FACTORY and USER banks, each
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
// escape closes. Wheel scrolls the pane under the mouse.
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

    void refresh();                                   // rebuild folders + rows

    // Select a folder programmatically (also used by tools/screenshots).
    // bank: -1 = ALL, 0 = FACTORY, 1 = USER; category empty = whole bank.
    // Returns false when no such folder exists (e.g. empty category).
    bool selectFolder (int bank, const juce::String& category);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
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
    juce::Rectangle<int> folderRect() const;          // outer bevel box, left
    juce::Rectangle<int> listRect() const;            // outer bevel box, right
    const Folder& selectedFolder() const;
    void rebuildFolders();
    void rebuildRows();                               // from the selected folder
    int rowAt (juce::Point<int> pos) const;
    int folderAt (juce::Point<int> pos) const;
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
    bool focusList = false;                           // key focus zone
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
