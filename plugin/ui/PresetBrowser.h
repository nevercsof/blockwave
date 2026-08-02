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

// Preset browser overlay: presets grouped by category (LEAD, BASS, PLUCK,
// PAD, KEYS, CHIP, PERC, FX) from PresetLibrary. Click loads (message
// thread -> atomic parameter writes), current preset highlighted, keyboard
// up/down + enter + escape. Each row keeps a 12x12 slot where the Phase-4
// mini craft-recipe icon will go.
//
// SavePanel: minimal pixel-art save dialog — name entry drawn with the
// bitmap font (own key handling, no native text editor), category cycler,
// SAVE / CANCEL.

#include "../PluginProcessor.h"
#include "ParamCells.h"

namespace blockwave::ui
{

class PresetBrowser final : public juce::Component
{
public:
    explicit PresetBrowser (BlockwaveAudioProcessor& processor);

    std::function<void (int)> onLoad;                 // library index
    std::function<void()> onClose;

    void refresh();                                   // rebuild rows

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    struct Row
    {
        bool header = false;
        juce::String text;
        int presetIndex = -1;
        bool isFactory = false;
    };

    juce::Rectangle<int> panelRect() const;
    juce::Rectangle<int> listRect() const;
    int rowAt (juce::Point<int> posInPanel) const;
    void moveCursor (int delta);
    void loadRow (int rowIndex);

    BlockwaveAudioProcessor& proc;
    juce::TextButton closeBtn { "X" };
    std::vector<Row> rows;
    int cursor = 0;                                   // keyboard cursor (row)
    int scrollY = 0;

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
