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

// BLOCKWAVE pixel-art editor (Phase 3). Fixed 832x456 base canvas with a
// 1x/2x integer scale toggle: the fixed-size content component gets an
// integer AffineTransform, so every procedurally drawn pixel stays crisp
// (all art is integer-coordinate fillRects + nearest-neighbour image blits).
//
// Tabs: CRAFT (default per SPEC — placeholder until Phase 4) and TWEAK
// (all 61 parameters). Top bar is always visible. Preset browser and save
// dialog are overlays. Everything runs on the message thread; parameter
// traffic goes exclusively through APVTS attachments.

#include "PluginProcessor.h"
#include "ui/PixelLookAndFeel.h"
#include "ui/TopBar.h"
#include "ui/TweakTab.h"
#include "ui/PresetBrowser.h"
#include "ui/CraftPlaceholder.h"

class BlockwaveAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    enum class Tab { craft, tweak };

    explicit BlockwaveAudioProcessorEditor (BlockwaveAudioProcessor&);
    ~BlockwaveAudioProcessorEditor() override;

    // Also used by tools/screenshots to render every screen offscreen.
    void setActiveTab (Tab);
    Tab getActiveTab() const { return activeTab; }
    void setUiScale (int scale);                      // 1 or 2, integer only
    int getUiScale() const { return uiScale; }
    void showPresetBrowser (bool shouldShow);
    void showSavePanel (bool shouldShow);
    blockwave::ui::TopBar& getTopBar() { return topBar; }

    void resized() override {}                        // fixed canvas

private:
    class Content final : public juce::Component
    {
    public:
        Content() { setOpaque (true); }
        void paint (juce::Graphics&) override;
    };

    BlockwaveAudioProcessor& proc;
    blockwave::ui::PixelLookAndFeel lnf;
    Content content;
    blockwave::ui::TopBar topBar;
    juce::TextButton craftTabBtn { "CRAFT" }, tweakTabBtn { "TWEAK" };
    blockwave::ui::CraftPlaceholder craftTab;
    blockwave::ui::TweakTab tweakTab;
    blockwave::ui::PresetBrowser browser;
    blockwave::ui::SavePanel savePanel;
    juce::TooltipWindow tooltips;

    Tab activeTab = Tab::craft;
    int uiScale = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockwaveAudioProcessorEditor)
};
