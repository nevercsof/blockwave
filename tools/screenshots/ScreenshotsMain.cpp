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

// Headless UI screenshot renderer (Phase 3 deliverable discipline, extended
// with the Phase 4 CRAFT screens): instantiates the processor + editor
// offscreen and renders every screen with Component::createComponentSnapshot
// — no window, no audio device — so look-and-feel can be reviewed without
// launching a host.
//
//   blockwave_screenshots [outputDir]     (default: CHECKPOINTS/screenshots)
//
// The discovery states drive the REAL code path (setCraftGrid -> recipe match
// -> DiscoveryStore -> UI poll -> toast), with the discovery file redirected
// to a temporary location so a run never touches the user's own
// Discoveries.json and always starts from zero found.

#include <iostream>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../plugin/PluginProcessor.h"
#include "../../plugin/PluginEditor.h"

namespace
{

void pump (int ms = 30)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

bool savePng (const juce::Image& img, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream (img, out);
}

bool shoot (juce::Component& comp, juce::Rectangle<int> area,
            const juce::File& file)
{
    auto img = comp.createComponentSnapshot (area, true, 1.0f);
    const bool ok = savePng (img, file);
    std::cout << (ok ? "wrote " : "FAILED ") << file.getFullPathName()
              << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
    return ok;
}

blockwave::Material mat (const char* name)
{
    blockwave::Material m {};
    blockwave::materialFromName (name, m);
    return m;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File outDir = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
        : juce::File::getCurrentWorkingDirectory()
              .getChildFile ("CHECKPOINTS/screenshots");
    outDir.createDirectory();

    bool ok = true;
    juce::File discoveryFile;
    {
        BlockwaveAudioProcessor proc;

        // Fresh, throw-away discovery state (never the user's real file).
        discoveryFile = juce::File::createTempFile ("blockwave_discoveries.json");
        discoveryFile.deleteFile();
        proc.getDiscoveries().setFile (discoveryFile);

        // Load the first factory preset so the top bar shows a real name.
        juce::String err;
        if (proc.getPresetLibrary().getNumPresets() > 0)
            proc.loadPresetAtIndex (0, err);

        std::unique_ptr<juce::AudioProcessorEditor> editorBase (proc.createEditor());
        auto* editor = dynamic_cast<BlockwaveAudioProcessorEditor*> (editorBase.get());
        if (editor == nullptr)
        {
            std::cerr << "editor is not a BlockwaveAudioProcessorEditor\n";
            return 1;
        }
        pump();

        using Tab = BlockwaveAudioProcessorEditor::Tab;

        // 1) CRAFT tab as it opens (default tab per SPEC), 1x and 2x.
        editor->setActiveTab (Tab::craft);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("craft_tab_1x.png")) && ok;
        editor->setUiScale (2);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("craft_tab_2x.png")) && ok;
        editor->setUiScale (1);
        pump();

        // 2) Bench loaded with materials (a plain craft, no recipe): the
        //    everyday CRAFT state — stacked ICE, mixed blocks, auto name.
        {
            blockwave::CraftGrid g;
            g.base = blockwave::CraftBase::LEAD;
            const char* cells[8] = { "ICE", "GOLD", "ICE", "VOLT",
                                     "SLIME", "", "CLOUD", "TNT" };
            for (int i = 0; i < blockwave::kNumCells; ++i)
                g.cells[i] = mat (cells[i]);
            proc.setCraftGrid (g);
            editor->getCraftTab().refreshFromProcessor();
            pump (120);
            ok = shoot (*editor, editor->getLocalBounds(),
                        outDir.getChildFile ("craft_grid_materials_1x.png")) && ok;
            ok = shoot (*editor, { 0, 64, 832, 392 },
                        outDir.getChildFile ("craft_bench_detail_1x.png")) && ok;
        }

        // 3) Discovery moment: craft an exact recipe pattern and let the UI
        //    poll pick it up (toast + counter), exactly as a user would.
        const auto& book = proc.getRecipeBook();
        if (book.getNumRecipes() > 0)
        {
            proc.setCraftGrid (book.getRecipe (0).pattern);
            editor->getCraftTab().refreshFromProcessor();
            pump (400);                       // 15 Hz timer: the toast is up
            ok = shoot (*editor, editor->getLocalBounds(),
                        outDir.getChildFile ("craft_discovery_toast_1x.png")) && ok;
            ok = shoot (*editor, { 400, 304, 440, 72 },
                        outDir.getChildFile ("craft_toast_detail_1x.png")) && ok;
        }

        // 4) Discoveries page with a few slots unlocked and the rest locked.
        for (int i = 1; i < juce::jmin (4, book.getNumRecipes()); ++i)
        {
            proc.setCraftGrid (book.getRecipe (i).pattern);
            editor->getCraftTab().refreshFromProcessor();
            pump (120);                       // let each discovery register
        }
        editor->getCraftTab().refreshFromProcessor();
        editor->getCraftTab().showDiscoveries (true);
        pump (120);
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("craft_discoveries_1x.png")) && ok;
        editor->getCraftTab().showDiscoveries (false);
        pump();

        // 5) TWEAK tab, 1x and 2x.
        editor->setActiveTab (Tab::tweak);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("tweak_tab_1x.png")) && ok;
        editor->setUiScale (2);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("tweak_tab_2x.png")) && ok;
        editor->setUiScale (1);
        pump();

        // 6) Top bar states: RAW off / on (cropped strip, 1x).
        const juce::Rectangle<int> topBarArea (0, 0, 832, 40);
        ok = shoot (*editor, topBarArea,
                    outDir.getChildFile ("topbar_raw_off_1x.png")) && ok;
        if (auto* raw = proc.apvts.getParameter ("raw"))
        {
            raw->setValueNotifyingHost (1.0f);
            pump();
            ok = shoot (*editor, topBarArea,
                        outDir.getChildFile ("topbar_raw_on_1x.png")) && ok;
            raw->setValueNotifyingHost (0.0f);
            pump();
        }

        // 7) Preset browser overlay — every row carries its mini 3x3 recipe
        //    icon (SPEC §UI); the second shot zooms in so the 12x12 icons are
        //    reviewable at 1x.
        editor->showPresetBrowser (true);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("preset_browser_1x.png")) && ok;
        ok = shoot (*editor, { 152, 60, 528, 208 },
                    outDir.getChildFile ("preset_browser_icons_1x.png")) && ok;
        editor->showPresetBrowser (false);
        pump();

        // 8) Mini recipe icon proof sheet: the same 12x12 renderer the preset
        //    browser uses, at true size and blown up 4x (nearest neighbour).
        //    The Phase-2 dev bank carries no craft data yet, so browser rows
        //    show the empty-well marker until the Phase 6 factory bank lands.
        {
            const int n = juce::jmin (8, book.getNumRecipes());
            juce::Image sheet (juce::Image::ARGB, 8 + n * 56, 96, true);
            {
                juce::Graphics g (sheet);
                g.fillAll (blockwave::ui::colours::night);
                for (int i = 0; i < n; ++i)
                {
                    const auto& rec = book.getRecipe (i);
                    const int x = 8 + i * 56;
                    blockwave::ui::drawMiniCraftIcon (g, rec.pattern, x, 8);
                    juce::Image big (juce::Image::ARGB, 12, 12, true);
                    { juce::Graphics bg (big);
                      blockwave::ui::drawMiniCraftIcon (bg, rec.pattern, 0, 0); }
                    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
                    g.drawImage (big, juce::Rectangle<float> (
                        static_cast<float> (x), 28.0f, 48.0f, 48.0f));
                    blockwave::ui::drawPixelText (g, rec.name.substring (0, 8),
                                                  x, 82, 1,
                                                  blockwave::ui::colours::dimText);
                }
            }
            ok = savePng (sheet, outDir.getChildFile ("craft_mini_icons_1x.png")) && ok;
            std::cout << "wrote craft_mini_icons_1x.png\n";
        }

        // 9) Save dialog, 1x.
        editor->showSavePanel (true);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("save_panel_1x.png")) && ok;
        editor->showSavePanel (false);
        pump();
    }

    discoveryFile.deleteFile();
    std::cout << (ok ? "all screenshots written\n" : "SOME SCREENSHOTS FAILED\n");
    return ok ? 0 : 1;
}
