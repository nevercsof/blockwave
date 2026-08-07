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
    setComponentID (TopBar::kIdScale);
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

int PixelScaleSlider::indexForMouseX (int x) noexcept
{
    // Snap to the nearest notch: the handle's own centre travels
    // kHandleW/2 .. kTrackW - kHandleW/2 in kNotchPitch steps.
    const int rel = x - kHandleW / 2;
    const int step = static_cast<int> (
        std::floor ((static_cast<float> (rel) + kNotchPitch / 2.0f) / kNotchPitch));
    return juce::jlimit (0, kNumSteps - 1, step);
}

void PixelScaleSlider::setScalePercent (int percent)
{
    // Authoritative display sync from the editor. Whatever gesture was in
    // flight loses: the scale it would have produced is already stale.
    const int next = indexForPercent (percent);
    const bool wasInFlight = pending >= 0 || dragging;
    pending = -1;
    dragging = false;
    wheelGestureStepped = false;
    stopTimer();
    if (next == index && ! wasInFlight)
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

void PixelScaleSlider::setPending (int newIndex)
{
    const int clamped = juce::jlimit (0, kNumSteps - 1, newIndex);
    if (clamped == pending)
        return;
    pending = clamped;
    repaint();                        // handle + readout follow the cursor
}

void PixelScaleSlider::abandonPending()
{
    if (pending < 0 && ! dragging)
    {
        wheelGestureStepped = false;
        return;
    }
    pending = -1;
    dragging = false;
    wheelGestureStepped = false;
    stopTimer();
    repaint();
}

void PixelScaleSlider::forgetLostDrag()
{
    // A gesture is only in flight while a button is actually held. If JUCE
    // never delivered the mouseUp (the host grabbed the mouse, the window went
    // away under the cursor, a modal panel swallowed the release) the stale
    // `dragging` would silently disable the wheel and the arrow keys forever.
    // Cheap to verify, so verify it at every entry point that trusts the flag
    // rather than pinning the recovery on a hook that may never fire.
    if (dragging && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        abandonPending();
}

void PixelScaleSlider::commitPendingScale()
{
    stopTimer();
    dragging = false;
    wheelGestureStepped = false;      // the spin is over: the next one may step
    if (pending < 0)
        return;
    const int target = pending;
    pending = -1;
    setIndex (target, true);          // the ONE callback of the whole gesture
    repaint();                        // clears the "not applied yet" styling
}

void PixelScaleSlider::timerCallback()
{
    // A pending step must never resize the canvas while a button is held —
    // anywhere, not just on this slider. See the note in TopBar.h: the grab
    // target keeps receiving drags measured against a layout that just moved,
    // and juce::Slider reads its drag start against the live layout, so the
    // master knob jumps by several per cent with no cursor motion. Defer, do
    // not drop: re-check every tick and apply on the first one with no button
    // down. Cheap, and it covers the long inertial tail of a trackpad flick as
    // well as the 140 ms case, because it asks about NOW rather than about how
    // long ago the spin was.
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        startTimer (kWheelCommitMs);
        return;
    }
    commitPendingScale();             // last event of a wheel spin wins
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

    // Everything below draws the DISPLAYED step: while a drag or a wheel spin
    // is in flight that is the pending one, so the control tracks the cursor
    // even though the window has not resized yet.
    const int shown = displayIndex();
    const bool inFlight = pending >= 0;

    // Filled portion up to the handle — "bigger pixels" reads as "more".
    const int handleX = shown * kNotchPitch;
    if (shown > 0)
    {
        g.setColour (grass);
        g.fillRect (track.getX() + 3, track.getCentreY() - 1, handleX, 2);
    }

    // While a step is pending, the notch the window is ACTUALLY at keeps an
    // ice pip, so "here now / landing there" is readable at a glance.
    if (inFlight && pending != index)
    {
        g.setColour (ice);
        g.fillRect (kHandleW / 2 - 1 + index * kNotchPitch, track.getY() + 3, 2, 8);
    }

    const juce::Rectangle<int> handle (handleX, 6, kHandleW, 22);
    drawBevelBox (g, handle, inFlight ? panelLight : buttonFace,
                  label, panelDark, outline);
    g.setColour (inFlight ? lava : ice);
    g.fillRect (handle.getCentreX() - 1, handle.getY() + 5, 2, 12);

    const juce::Rectangle<int> readout (kTrackW + 8, 8, kReadoutW, 16);
    drawBevelBox (g, readout, chip, panelDark, panelLight, outline, true);
    drawPixelTextCentred (g, juce::String (kStepPercent[shown]) + "%", readout, 1,
                          inFlight ? lava : ice);

    if (hasKeyboardFocus (false))
        drawFocusTicks (g, { 0, 4, width, height - 4 });
}

void PixelScaleSlider::mouseDown (const juce::MouseEvent& e)
{
    stopTimer();                       // a grab beats an unfinished wheel spin
    pending = -1;
    dragging = false;
    wheelGestureStepped = false;

    // A right-press (or macOS ctrl+left) is a context gesture, never a value
    // change — juce::Slider does not take a value from one and neither does
    // this. It must not start a drag whose release would silently resize the
    // whole window, and it must not steal the keyboard focus either.
    if (e.mods.isPopupMenu())
    {
        repaint();
        return;
    }

    grabKeyboardFocus();
    if (e.getPosition().x > kTrackW)   // pressed on the readout chip: no drag
    {
        repaint();
        return;
    }
    dragging = true;
    setPending (indexForMouseX (e.getPosition().x));
}

void PixelScaleSlider::mouseDrag (const juce::MouseEvent& e)
{
    // PREVIEW ONLY — see the loop note in TopBar.h. Firing onScaleChange here
    // rescales the component the cursor is being measured against, and the
    // window ping-pongs between two sizes until the button comes back up.
    if (! dragging)
        return;
    setPending (indexForMouseX (e.getPosition().x));
}

void PixelScaleSlider::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;
    commitPendingScale();              // the gesture's one and only apply
}

void PixelScaleSlider::wheelStep (bool up, bool smoothGesture, bool inertialTail)
{
    forgetLostDrag();
    if (dragging)
        return;

    // ONE GESTURE, ONE STEP for anything continuous — see TopBar.h. The
    // momentum tail is never a step of its own, and a trackpad burst takes its
    // one step on the first event; only a detented wheel accumulates, because
    // there every event is a click the user physically made.
    if (inertialTail)
    {
        if (pending >= 0)
            startTimer (kWheelCommitMs);   // keep the open gesture open
        return;
    }

    if (smoothGesture && wheelGestureStepped)
    {
        startTimer (kWheelCommitMs);       // same flick, no further step
        return;
    }

    wheelGestureStepped = true;
    setPending (displayIndex() + (up ? 1 : -1));
    startTimer (kWheelCommitMs);       // one apply per spin, on the last event
}

void PixelScaleSlider::mouseWheelMove (const juce::MouseEvent&,
                                       const juce::MouseWheelDetails& w)
{
    if (w.deltaY == 0.0f)
        return;
    wheelStep (w.deltaY > 0.0f, w.isSmooth, w.isInertial);
}

bool PixelScaleSlider::keyPressed (const juce::KeyPress& k)
{
    forgetLostDrag();

    // One discrete step per press, applied immediately: there is no position
    // being sampled, so nothing here can feed back into itself. A step that
    // lands on top of an uncommitted wheel step just replaces it.
    //
    // ...EXCEPT while a track drag is in flight. mouseDown grabbed the focus,
    // so during a drag this slider owns the keyboard: an arrow key (or its
    // auto-repeat) would apply a step, rescale the canvas, and move the slider
    // under the still-held cursor — the exact positional feedback loop the
    // deferral exists to kill, re-entered through the other input device. A
    // held button owns the control; keys wait their turn. They are still
    // CONSUMED so they cannot leak to another handler mid-gesture.
    const int from = displayIndex();
    int target = from;

    if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::upKey))
        target = from + 1;
    else if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::downKey))
        target = from - 1;
    else if (k.isKeyCode (juce::KeyPress::homeKey))
        target = 0;
    else if (k.isKeyCode (juce::KeyPress::endKey))
        target = kNumSteps - 1;
    else
        return false;

    if (dragging)
        return true;                  // consumed and ignored: see above

    pending = -1;
    wheelGestureStepped = false;
    stopTimer();
    // No aim is recorded here, or anywhere else in this class. The editor's
    // settle guard reads the LIVE pointer when it applies the transform, so a
    // keyboard commit is guarded exactly like a pointer one: if the canvas
    // moves a hazard under a parked cursor, the device that asked for the move
    // is irrelevant.
    setIndex (target, true);
    repaint();
    return true;
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

    prevBtn.setComponentID (kIdPrev);
    prevBtn.setTooltip ("previous preset");
    prevBtn.onClick = [this] { stepPreset (false); };
    addAndMakeVisible (prevBtn);

    nextBtn.setComponentID (kIdNext);
    nextBtn.setTooltip ("next preset");
    nextBtn.onClick = [this] { stepPreset (true); };
    addAndMakeVisible (nextBtn);

    nameBtn.setComponentID (kIdName);
    nameBtn.setTooltip ("preset browser");
    nameBtn.onClick = [this] { if (onBrowse) onBrowse(); };
    addAndMakeVisible (nameBtn);

    browseBtn.setComponentID (kIdBrowse);
    browseBtn.setTooltip ("preset browser");
    browseBtn.onClick = [this] { if (onBrowse) onBrowse(); };
    addAndMakeVisible (browseBtn);

    saveBtn.setComponentID (kIdSave);
    saveBtn.setTooltip ("save user preset");
    saveBtn.onClick = [this] { if (onSave) onSave(); };
    addAndMakeVisible (saveBtn);

    favBtn.setComponentID (kIdFav);
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

    rawBtn.setComponentID (kIdRaw);
    rawBtn.setTooltip ("aliased retro dirt");
    addAndMakeVisible (rawBtn);
    rawAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "raw", rawBtn);

    masterKnob.setComponentID (kIdMaster);
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
