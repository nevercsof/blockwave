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

#include "TopBar.h"
#include <cmath>
#include <cstdlib>

namespace blockwave::ui
{

// ---- PixelScaleSlider --------------------------------------------------------

PixelScaleSlider::PixelScaleSlider()
{
    setWantsKeyboardFocus (true);
    setTooltip ("pixel size steps");
}

int PixelScaleSlider::indexForPercent (int percent) noexcept
{
    int best = 0, bestErr = 1 << 20;
    for (int i = 0; i < kNumSteps; ++i)
    {
        const int err = std::abs (kStepPercent[i] - percent);
        if (err < bestErr) { bestErr = err; best = i; }
    }
    return best;
}

juce::Rectangle<int> PixelScaleSlider::trackBounds() const
{
    return { 0, 10, kTrackW, 14 };
}

void PixelScaleSlider::setScalePercent (int percent)
{
    const int next = indexForPercent (percent);
    if (next == index)
        return;
    index = next;
    repaint();
}

void PixelScaleSlider::setIndex (int newIndex, bool notify)
{
    const int clamped = juce::jlimit (0, kNumSteps - 1, newIndex);
    if (clamped == index)
        return;
    index = clamped;
    repaint();
    if (notify && onScaleChange)
        onScaleChange (kStepPercent[index]);
}

void PixelScaleSlider::setFromMouse (int x)
{
    // Snap to the nearest notch: the handle's own centre travels
    // kHandleW/2 .. kTrackW - kHandleW/2 in kNotchPitch steps.
    const int rel = x - kHandleW / 2;
    setIndex ((rel + kNotchPitch / 2) / kNotchPitch, true);
}

void PixelScaleSlider::paint (juce::Graphics& g)
{
    using namespace colours;
    const auto track = trackBounds();

    drawPixelText (g, "SCALE", 0, 0, 1, dimText);

    drawBevelBox (g, track, chip, panelDark, panelLight, outline, true);

    // Notch ticks inside the well: five stops you can see and aim at.
    g.setColour (panelDark);
    for (int i = 0; i < kNumSteps; ++i)
        g.fillRect (kHandleW / 2 - 1 + i * kNotchPitch, track.getY() + 4, 2, 6);

    // Filled portion up to the handle — "bigger pixels" reads as "more".
    const int handleX = index * kNotchPitch;
    if (index > 0)
    {
        g.setColour (grass);
        g.fillRect (track.getX() + 3, track.getCentreY() - 1, handleX, 2);
    }

    const juce::Rectangle<int> handle (handleX, 6, kHandleW, 22);
    drawBevelBox (g, handle, buttonFace, label, panelDark, outline);
    g.setColour (ice);
    g.fillRect (handle.getCentreX() - 1, handle.getY() + 5, 2, 12);

    const juce::Rectangle<int> readout (kTrackW + 8, 8, kReadoutW, 16);
    drawBevelBox (g, readout, chip, panelDark, panelLight, outline, true);
    drawPixelTextCentred (g, juce::String (kStepPercent[index]) + "%", readout, 1, ice);

    if (hasKeyboardFocus (false))
        drawFocusTicks (g, { 0, 4, width, height - 4 });
}

void PixelScaleSlider::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.getPosition().x <= kTrackW)
        setFromMouse (e.getPosition().x);
}

void PixelScaleSlider::mouseDrag (const juce::MouseEvent& e)
{
    setFromMouse (e.getPosition().x);
}

void PixelScaleSlider::mouseWheelMove (const juce::MouseEvent&,
                                       const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f)
        setIndex (index + (w.deltaY > 0.0f ? 1 : -1), true);
}

bool PixelScaleSlider::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::upKey))
    {
        setIndex (index + 1, true);
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::downKey))
    {
        setIndex (index - 1, true);
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::homeKey)) { setIndex (0, true); return true; }
    if (k.isKeyCode (juce::KeyPress::endKey))  { setIndex (kNumSteps - 1, true); return true; }
    return false;
}

// ---- StarButton --------------------------------------------------------------

void StarButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace colours;
    // Sunken chip well so the star sits in the bar like the other controls.
    drawBevelBox (g, getLocalBounds(), chip, panelDark, panelLight, outline, true);
    const int x = (getWidth() - 8) / 2 + (down ? 1 : 0);
    const int y = (getHeight() - 8) / 2 + (down ? 1 : 0);
    drawPixelStar (g, x, y, lit, starGold,
                   highlighted ? label : panelFace);
    if (hasKeyboardFocus (false))
        drawFocusTicks (g, getLocalBounds());
}

TopBar::TopBar (BlockwaveAudioProcessor& processor) : proc (processor)
{
    setOpaque (true);

    prevBtn.setTooltip ("previous preset");
    prevBtn.onClick = [this] { stepPreset (false); };
    addAndMakeVisible (prevBtn);

    nextBtn.setTooltip ("next preset");
    nextBtn.onClick = [this] { stepPreset (true); };
    addAndMakeVisible (nextBtn);

    nameBtn.setComponentID ("namebox");
    nameBtn.setTooltip ("preset browser");
    nameBtn.onClick = [this] { if (onBrowse) onBrowse(); };
    addAndMakeVisible (nameBtn);

    browseBtn.setTooltip ("preset browser");
    browseBtn.onClick = [this] { if (onBrowse) onBrowse(); };
    addAndMakeVisible (browseBtn);

    saveBtn.setTooltip ("save user preset");
    saveBtn.onClick = [this] { if (onSave) onSave(); };
    addAndMakeVisible (saveBtn);

    favBtn.setTooltip ("star this sound");
    favBtn.onClick = [this]
    {
        auto& lib = proc.getPresetLibrary();
        lib.toggleFavorite (lib.getCurrentIndex());
        refresh();
        if (onFavoriteToggled)
            onFavoriteToggled();
    };
    addAndMakeVisible (favBtn);

    rawBtn.setComponentID ("led");
    rawBtn.setTooltip ("aliased retro dirt");
    addAndMakeVisible (rawBtn);
    rawAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "raw", rawBtn);

    masterKnob.setTooltip ("final output level");
    masterKnob.setWantsKeyboardFocus (true);
    masterKnob.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (masterKnob);
    masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, "master_gain", masterKnob);

    scaleSlider.onScaleChange = [this] (int percent)
    {
        if (onScaleChange)
            onScaleChange (percent);
    };
    addAndMakeVisible (scaleSlider);

    refresh();
}

void TopBar::stepPreset (bool forward)
{
    juce::String err;
    if (forward)
        proc.loadNextPreset (err);
    else
        proc.loadPrevPreset (err);
    refresh();
    if (onPresetChanged)
        onPresetChanged();
}

void TopBar::refresh()
{
    const auto name = proc.getPresetName();
    const auto cat  = proc.getPresetCategory();
    nameBtn.setButtonText (cat.isNotEmpty() ? cat + " : " + name : name);
    auto& lib = proc.getPresetLibrary();
    favBtn.setLit (lib.isFavorite (lib.getCurrentIndex()));
    repaint();
}

void TopBar::setScalePercent (int s)
{
    scaleSlider.setScalePercent (s);                   // shows the CURRENT scale
}

void TopBar::paint (juce::Graphics& g)
{
    using namespace colours;
    g.fillAll (night);

    // Logo, bitmap font at 2x with a 1-px night shadow: BLOCK grass, WAVE ice.
    const int ly = (kTopBarH - 12) / 2;
    drawPixelText (g, "BLOCK", 9, ly + 1, 2, outline);
    const int lw = drawPixelText (g, "BLOCK", 8, ly, 2, grass);
    drawPixelText (g, "WAVE", 8 + lw + 1, ly + 1, 2, outline);
    drawPixelText (g, "WAVE", 8 + lw, ly, 2, ice);

    // Lava accent line under the bar.
    g.setColour (lava);
    g.fillRect (0, kTopBarH - 2, getWidth(), 2);
}

void TopBar::resized()
{
    // Three groups on the 8-px grid, each separated by 40 px of night:
    //   presets 88..448 | sound 488..584 | view 624..824.
    // RAW and the master knob moved left out of the old 184 px hole so the
    // scale slider gets the right end without crowding anything.
    prevBtn.setBounds   (88,  12, 16, 16);
    nameBtn.setBounds   (108, 12, 168, 16);
    nextBtn.setBounds   (280, 12, 16, 16);
    browseBtn.setBounds (304, 12, 64, 16);
    saveBtn.setBounds   (376, 12, 48, 16);
    favBtn.setBounds    (432, 12, 16, 16);
    rawBtn.setBounds    (488, 12, 56, 16);
    masterKnob.setBounds (560, 8, 24, 24);
    scaleSlider.setBounds (624, 6, PixelScaleSlider::width, PixelScaleSlider::height);
}

} // namespace blockwave::ui
