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

// Headless UI screenshot renderer (Phase 3 deliverable discipline):
// instantiates the processor + editor offscreen and renders every screen
// with Component::createComponentSnapshot — no window, no audio device —
// so look-and-feel can be reviewed without launching a host.
//
//   blockwave_screenshots [outputDir]     (default: CHECKPOINTS/screenshots)

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
    {
        BlockwaveAudioProcessor proc;

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

        // 1) CRAFT tab placeholder (default tab per SPEC), 1x.
        editor->setActiveTab (BlockwaveAudioProcessorEditor::Tab::craft);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("craft_tab_1x.png")) && ok;

        // 2) TWEAK tab, 1x and 2x.
        editor->setActiveTab (BlockwaveAudioProcessorEditor::Tab::tweak);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("tweak_tab_1x.png")) && ok;
        editor->setUiScale (2);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("tweak_tab_2x.png")) && ok;
        editor->setUiScale (1);
        pump();

        // 3) Top bar states: RAW off / on (cropped strip, 1x).
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

        // 4) Preset browser overlay (over TWEAK), 1x.
        editor->showPresetBrowser (true);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("preset_browser_1x.png")) && ok;
        editor->showPresetBrowser (false);
        pump();

        // 5) Save dialog, 1x.
        editor->showSavePanel (true);
        pump();
        ok = shoot (*editor, editor->getLocalBounds(),
                    outDir.getChildFile ("save_panel_1x.png")) && ok;
        editor->showSavePanel (false);
        pump();
    }

    std::cout << (ok ? "all screenshots written\n" : "SOME SCREENSHOTS FAILED\n");
    return ok ? 0 : 1;
}
