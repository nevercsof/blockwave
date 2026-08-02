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
// 100/125/150/175/200 % scale cycler: the fixed-size content component gets
// an AffineTransform, so all layout/art code stays in canvas coordinates.
// 100% and 200% are exact integer transforms (pixel-perfect); the fractional
// steps keep every mark a solid chunky block (integer-coordinate fillRects +
// low-quality/nearest-neighbour image blits — uneven pixel sizes, no
// smoothing filter). Scale persists as the session property "uiScale"
// (percent; values < 10 are legacy 1x/2x integers, migrated on read).
//
// Tabs: CRAFT (default per SPEC — the crafting bench, Phase 4) and TWEAK
// (all 61 parameters). Top bar is always visible. Preset browser and save
// dialog are overlays. Everything runs on the message thread; parameter
// traffic goes exclusively through APVTS attachments.

#include "PluginProcessor.h"
#include "ui/PixelLookAndFeel.h"
#include "ui/TopBar.h"
#include "ui/TweakTab.h"
#include "ui/PresetBrowser.h"
#include "ui/CraftTab.h"

class BlockwaveAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    enum class Tab { craft, tweak };

    explicit BlockwaveAudioProcessorEditor (BlockwaveAudioProcessor&);
    ~BlockwaveAudioProcessorEditor() override;

    // Also used by tools/screenshots to render every screen offscreen.
    void setActiveTab (Tab);
    Tab getActiveTab() const { return activeTab; }
    void setUiScale (int scalePercent);               // snapped to 100..200 by 25
    int getUiScale() const { return uiScale; }        // percent
    void showPresetBrowser (bool shouldShow);
    void showSavePanel (bool shouldShow);
    blockwave::ui::TopBar& getTopBar() { return topBar; }
    blockwave::ui::CraftTab& getCraftTab() { return craftTab; }
    blockwave::ui::PresetBrowser& getBrowser() { return browser; }

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
    blockwave::ui::CraftTab craftTab;
    blockwave::ui::TweakTab tweakTab;
    blockwave::ui::PresetBrowser browser;
    blockwave::ui::SavePanel savePanel;
    juce::TooltipWindow tooltips;

    Tab activeTab = Tab::craft;
    int uiScale = 100;                                // percent

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockwaveAudioProcessorEditor)
};
