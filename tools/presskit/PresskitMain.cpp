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

// Press-kit image renderer. Sibling of tools/screenshots: same headless
// offscreen technique (Component::createComponentSnapshot, no window, no
// audio device), same store redirection so a run never touches the user's
// real Discoveries.json / Favorites.json / Settings.json.
//
//   blockwave_presskit frames [outDir]   numbered PNG frame sequence of ONE
//                                        real craft, empty bench -> three
//                                        blocks -> recipe discovery, plus a
//                                        frames.txt manifest of per-frame
//                                        durations for the GIF assembler.
//   blockwave_presskit logo   [outDir]   wordmark / icon pack, drawn with the
//                                        product's own PixelFont + PixelTheme
//                                        so the letterforms are literally the
//                                        ones in the plug-in's top bar.
//
// The frame run drives the PRODUCTION gestures: a palette click arms a
// material, a bench click places it (placement flash included), the grid edit
// goes to the processor, the processor matches the recipe and registers the
// discovery, and the CRAFT tab's own 15 Hz poll raises the toast. Nothing in
// the sequence is staged; every frame is the real UI in a real state.

#include <iostream>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../plugin/PluginProcessor.h"
#include "../../plugin/PluginEditor.h"
#include "../../plugin/GlobalSettings.h"
#include "../../plugin/ui/PixelFont.h"
#include "../../plugin/ui/PixelTheme.h"

namespace
{

constexpr int kTickMs = 67;          // one CraftTab timer tick at 15 Hz

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

// True 1x pixels blown up nearest-neighbour. No resampling, no invented
// pixels — the same helper tools/screenshots uses for its zoom sheets.
juce::Image zoomed (const juce::Image& src, int factor)
{
    juce::Image out (juce::Image::ARGB, src.getWidth() * factor,
                     src.getHeight() * factor, true);
    juce::Graphics g (out);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (src, juce::Rectangle<float> (0.0f, 0.0f,
                                              static_cast<float> (out.getWidth()),
                                              static_cast<float> (out.getHeight())));
    return out;
}

// ---------------------------------------------------------------------------
// Frame sequence
// ---------------------------------------------------------------------------

struct FrameWriter
{
    juce::File dir;
    juce::StringArray manifest;
    int index = 0;
    bool ok = true;

    // Snapshots the editor and records how long the GIF should hold it.
    void grab (juce::Component& comp, int holdMs)
    {
        auto img = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);
        auto file = dir.getChildFile (juce::String::formatted ("frame_%04d.png", index));
        ok = savePng (img, file) && ok;
        manifest.add (file.getFileName() + " " + juce::String (holdMs));
        ++index;
    }

    // Records `ticks` consecutive timer ticks, one frame each, so a 2-4 frame
    // pixel animation lands in the GIF exactly as the UI plays it.
    void grabTicks (juce::Component& comp, int ticks)
    {
        for (int i = 0; i < ticks; ++i)
        {
            pump (kTickMs);
            grab (comp, kTickMs);
        }
    }

    bool writeManifest() const
    {
        return dir.getChildFile ("frames.txt")
                  .replaceWithText (manifest.joinIntoString ("\n") + "\n");
    }
};

int runFrames (const juce::File& outDir)
{
    outDir.createDirectory();
    for (const auto& stale : outDir.findChildFiles (juce::File::findFiles, false, "frame_*.png"))
        stale.deleteFile();

    auto settingsFile = juce::File::createTempFile ("blockwave_pk_settings.json");
    settingsFile.deleteFile();
    blockwave::GlobalSettings::setDefaultFile (settingsFile);

    bool ok = true;
    juce::File discoveryFile, favoritesFile;

    {
        BlockwaveAudioProcessor proc;

        discoveryFile = juce::File::createTempFile ("blockwave_pk_discoveries.json");
        discoveryFile.deleteFile();
        proc.getDiscoveries().setFile (discoveryFile);

        favoritesFile = juce::File::createTempFile ("blockwave_pk_favorites.json");
        favoritesFile.deleteFile();
        proc.getPresetLibrary().setFavoritesFile (favoritesFile);

        std::unique_ptr<juce::AudioProcessorEditor> editorBase (proc.createEditor());
        auto* editor = dynamic_cast<BlockwaveAudioProcessorEditor*> (editorBase.get());
        if (editor == nullptr)
        {
            std::cerr << "editor is not a BlockwaveAudioProcessorEditor\n";
            return 1;
        }
        editor->setActiveTab (BlockwaveAudioProcessorEditor::Tab::craft);
        pump (200);

        auto& craft = editor->getCraftTab();

        // The recipe to show is recipe 0 of the shipped book: the flagship,
        // and one of the three names that may appear in public material.
        const auto& book = proc.getRecipeBook();
        if (book.getNumRecipes() == 0)
        {
            std::cerr << "recipe book is empty\n";
            return 1;
        }
        const auto& target = book.getRecipe (0);
        std::cout << "recipe shown: " << target.name << "\n";

        // ---- set the bench to the empty starting state ---------------------
        craft.clearBenchForDisplay();
        pump (200);
        {
            // Walk the base selector to the recipe's base through the same
            // call the < > buttons make.
            const int want = static_cast<int> (target.pattern.base);
            for (int guard = 0; guard < blockwave::kNumBases; ++guard)
            {
                if (static_cast<int> (craft.getShownGrid().base) == want)
                    break;
                craft.cycleBaseForDisplay (1);
                pump (40);
            }
        }
        craft.clearBenchForDisplay();
        pump (400);                                     // let the flashes die

        FrameWriter fw { outDir, {}, 0, true };

        // 1) The empty bench, held long enough to read the hint line.
        fw.grab (*editor, 900);

        // 2) Place every material the pattern asks for, one block at a time:
        //    click the palette tile (it lights up, the hint changes), then
        //    click the slot (placement flash, grid edit, patch renamed).
        std::vector<int> filled;
        for (int slot = 0; slot < blockwave::kNumCells; ++slot)
            if (target.pattern.cells[slot] != blockwave::Material::none)
                filled.push_back (slot);

        blockwave::Material armed = blockwave::Material::none;
        for (size_t n = 0; n < filled.size(); ++n)
        {
            const int slot = filled[n];
            const auto m = target.pattern.cells[slot];
            const bool last = (n + 1 == filled.size());

            if (m != armed)
            {
                craft.clickMaterialForDisplay (m);      // arm it
                armed = m;
                pump (40);
                fw.grab (*editor, 550);                 // tile lit, hint reads
            }

            craft.clickCellForDisplay (slot, false);    // -> placeMaterial
            if (last)
            {
                // Put the palette back down (a second click on the lit tile is
                // exactly how a player disarms it) so the payoff frames are
                // the bench and the toast, not a leftover highlight.
                craft.clickMaterialForDisplay (m);
                armed = blockwave::Material::none;
                continue;
            }
            fw.grabTicks (*editor, 3);                  // 2-frame place flash
            fw.grab (*editor, 420);                     // settle on the block
        }

        // 3) The discovery. The toast is raised by the tab's own 15 Hz poll,
        //    so from here the sequence is just "keep taking one frame per
        //    tick" — slide-in, sparkle, the gold name, the counter.
        fw.grabTicks (*editor, 41);                     // flash + 3 in + hold + out
        std::cout << "  discoveries after craft: "
                  << proc.getDiscoveries().getNumFound() << "\n"
                  << "  patch name: " << proc.getCraftAutoName() << "\n";

        // 4) Hold the discovered bench (gold name, counter ticked) so a reader
        //    who missed the toast still gets the point.
        fw.grab (*editor, 1400);

        // 5) Clear back to an empty bench so the loop closes on the state it
        //    opened on. Only the counter differs, which is the whole story.
        craft.clearBenchForDisplay();
        fw.grabTicks (*editor, 3);
        fw.grab (*editor, 900);

        ok = fw.ok && fw.writeManifest() && ok;
        std::cout << "wrote " << fw.index << " frames to "
                  << outDir.getFullPathName() << "\n";
    }

    discoveryFile.deleteFile();
    favoritesFile.deleteFile();
    settingsFile.deleteFile();
    blockwave::GlobalSettings::setDefaultFile ({});
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Logo pack
// ---------------------------------------------------------------------------

// The top bar's wordmark, pixel for pixel (PluginEditor.cpp): BLOCK in grass,
// WAVE in ice, each with a 1-px outline drop shadow. Returns the drawn width.
int drawWordmark (juce::Graphics& g, int x, int y, int scale)
{
    using namespace blockwave::ui;
    using namespace blockwave::ui::colours;

    drawPixelText (g, "BLOCK", x + 1, y + 1, scale, outline);
    const int lw = drawPixelText (g, "BLOCK", x, y, scale, grass);
    drawPixelText (g, "WAVE", x + lw + 1, y + 1, scale, outline);
    drawPixelText (g, "WAVE", x + lw, y, scale, ice);
    return lw + pixelTextWidth ("WAVE", scale);
}

int wordmarkWidth (int scale)
{
    using namespace blockwave::ui;
    return pixelTextWidth ("BLOCK", scale) + pixelTextWidth ("WAVE", scale);
}

// Original chunky square wave: two cycles, `thick` px of ink, drawn with
// fillRect on integer coordinates only. Nothing traced, nothing sampled.
void drawSquareWave (juce::Graphics& g, int x, int y, int w, int h,
                     int thick, juce::Colour colour)
{
    g.setColour (colour);
    const int quarter = w / 4;                      // two full cycles
    const int lo = y + h - thick;
    for (int i = 0; i < 4; ++i)
    {
        const int sx = x + i * quarter;
        const int top = (i % 2 == 0) ? y : lo;
        g.fillRect (sx, top, quarter, thick);       // horizontal run
        g.fillRect (sx, y, thick, h);               // vertical edge
    }
    g.fillRect (x + w - thick, y, thick, h);        // closing edge
}

int runLogo (const juce::File& outDir)
{
    using namespace blockwave::ui;
    using namespace blockwave::ui::colours;

    outDir.createDirectory();
    bool ok = true;

    auto emit = [&] (const juce::Image& img, const char* stem, int big)
    {
        ok = savePng (img, outDir.getChildFile (juce::String (stem) + "_1x.png")) && ok;
        ok = savePng (zoomed (img, big),
                      outDir.getChildFile (juce::String (stem) + "_" + juce::String (big) + "x.png")) && ok;
        std::cout << "wrote " << stem << "_1x.png (" << img.getWidth() << "x"
                  << img.getHeight() << ") and " << stem << "_" << big << "x.png ("
                  << img.getWidth() * big << "x" << img.getHeight() * big << ")\n";
    };

    // ---- 1) wordmark, transparent -----------------------------------------
    // Master is the top bar's own scale-2 wordmark in an 8-px-grid box.
    const int wmScale = 2;
    const int wmW = wordmarkWidth (wmScale) + 1;         // +1 for the shadow
    const int wmH = pixelTextHeight (wmScale) + 1;
    const int boxW = 80, boxH = 24;
    jassert (wmW <= boxW && wmH <= boxH);
    const int wx = (boxW - wmW) / 2, wy = (boxH - wmH) / 2;

    juce::Image wordTransparent (juce::Image::ARGB, boxW, boxH, true);
    { juce::Graphics g (wordTransparent); drawWordmark (g, wx, wy, wmScale); }
    emit (wordTransparent, "blockwave_wordmark_transparent", 8);

    // ---- 2) wordmark on the night background -------------------------------
    juce::Image wordNight (juce::Image::ARGB, boxW, boxH, true);
    {
        juce::Graphics g (wordNight);
        g.fillAll (night);
        drawWordmark (g, wx, wy, wmScale);
    }
    emit (wordNight, "blockwave_wordmark_night", 8);

    // ---- 3) square avatar icon ---------------------------------------------
    // 128 x 128 (16 x 8-px cells): a bevelled block panel in the UI's own
    // materials, the wordmark stacked at scale 4, and an original square wave
    // under it. Every element is a fillRect on an integer coordinate.
    const int icoN = 128;
    juce::Image icon (juce::Image::ARGB, icoN, icoN, true);
    {
        juce::Graphics g (icon);
        g.fillAll (night);

        // Bevelled frame: light top/left, dark bottom/right, 1-px outline —
        // the same drawBevelBox the panels use, face left as night.
        drawBevelBox (g, { 0, 0, icoN, icoN }, night, panelFace, panelDark,
                      outline, false, 3);

        // Deterministic speckle, the CRAFT background's texture trick.
        juce::Random rng (0xb10c);
        g.setColour (speckle);
        for (int i = 0; i < 90; ++i)
        {
            const int sx = 8 + rng.nextInt (icoN - 20);
            const int sy = 8 + rng.nextInt (icoN - 20);
            g.fillRect (sx, sy, 2, 2);
        }

        const int s = 5;
        const int bw = pixelTextWidth ("BLOCK", s);
        const int ww = pixelTextWidth ("WAVE", s);
        const int lineH = pixelTextHeight (s);

        const int by = 18, wyy = by + lineH + 6;
        drawPixelText (g, "BLOCK", (icoN - bw) / 2 + 1, by + 1, s, outline);
        drawPixelText (g, "BLOCK", (icoN - bw) / 2, by, s, grass);
        drawPixelText (g, "WAVE", (icoN - ww) / 2 + 1, wyy + 1, s, outline);
        drawPixelText (g, "WAVE", (icoN - ww) / 2, wyy, s, ice);

        drawSquareWave (g, 24, wyy + lineH + 12, 80, 22, 4, lava);
    }
    emit (icon, "blockwave_icon", 8);

    return ok ? 0 : 1;
}

} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String mode = argc > 1 ? juce::String (argv[1]) : juce::String ("frames");
    const juce::String outArg = argc > 2 ? juce::String (argv[2])
                              : juce::String ("launch/presskit/") + mode;
    const auto outDir = juce::File::isAbsolutePath (outArg)
                      ? juce::File (outArg)
                      : juce::File::getCurrentWorkingDirectory().getChildFile (outArg);

    if (mode == "frames")
        return runFrames (outDir);
    if (mode == "logo")
        return runLogo (outDir);

    std::cerr << "usage: blockwave_presskit [frames|logo] [outDir]\n";
    return 2;
}
