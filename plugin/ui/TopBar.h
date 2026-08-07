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

// Always-visible top bar (SPEC §UI): logo, preset prev/next, preset name
// (click opens the browser), PRESETS, SAVE, favourite star, RAW LED toggle,
// master volume knob, UI SCALE slider (100/125/150/175/200 %). Preset loading
// runs on the message thread and lands as atomic parameter writes (via the
// processor's preset API).
//
// The scale control is a 5-notch SLIDER, not the old cycling button
// (producer's call: the percent scheme stays, the interaction becomes direct —
// you can see all five stops and jump straight to one instead of clicking
// through four to get back). Layout: the bar had a 184 px hole between the
// star and RAW, so the sound group (RAW + master) moved left into it and the
// 200 px scale group took the right end — no control got smaller and every
// group is separated by 40 px of night.

#include "../PluginProcessor.h"
#include "ParamCells.h"

namespace blockwave::ui
{

// Horizontal pixel slider with 5 discrete notches and a percent readout.
// Not a juce::Slider: the value is UI-only (never a parameter), the steps are
// a fixed list, and the whole thing must be drawn as flat blocks.
//
// ---- WHY THE SCALE IS NEVER APPLIED WHILE THE MOUSE IS DOWN ----------------
// This slider LIVES INSIDE the component it rescales. `onScaleChange` reaches
// BlockwaveAudioProcessorEditor::setUiScale, which calls content.setTransform()
// + setSize() — and `content` is this slider's own parent chain. Applying a
// step mid-drag therefore moves and rescales the slider UNDER a stationary
// cursor: the same physical mouse position maps to a different logical x, that
// x snaps to a different notch, that notch applies a different transform, and
// the window oscillates between two sizes for as long as the button is held.
// A textbook positional feedback loop (reported by the producer: "начинает
// резко меняться туда-сюда размер").
//
// The fix is structural, not a damping hack: a drag only ever moves a PENDING
// index. The handle and the percent readout follow the cursor live so the user
// still sees what they are choosing, but `onScaleChange` fires exactly once,
// on mouseUp, and only if the committed step actually changed. JUCE routes
// every drag/up of a gesture back to the component that took the mouseDown,
// so the slider keeps receiving them wherever it has moved to; and the arrow
// keys — which the slider owns for the whole drag, because mouseDown grabbed
// the focus — are inert while `dragging`, so no second input path can commit
// a step underneath the one being aimed.
//
// The mouse wheel has no position to corrupt, so it cannot re-enter the same
// way — but a single trackpad flick delivers a burst of wheel events, and
// applying each one would fire a burst of window resizes. Wheel steps are
// therefore coalesced behind a short one-shot timer and applied on the last
// event of the spin. Arrow keys are one discrete step per press and apply
// immediately.
//
// ---- ONE GESTURE, ONE STEP (for anything continuous) -----------------------
// Coalescing the APPLY was only half of it. Each event still added a STEP, and
// a macOS trackpad reports a gentle two-finger flick as dozens of events (then
// dozens more of inertial tail, all routed to the original wheel target — see
// the note below), so `pending` saturated at the top notch and one flick took
// the window from 100 % straight to 200 %. Counting events counts the
// hardware's sampling rate, not the user's intent.
//
// So the step count is keyed on the KIND of gesture, which is the thing that
// actually differs, and JUCE hands it to us on every event:
//   - `isInertial` — the momentum tail after the fingers lift. Never a step of
//     its own; it only keeps an open gesture open so the one step it belongs
//     to still lands on the last event.
//   - `isSmooth` (trackpad, Magic Mouse: precise deltas) — a continuous
//     gesture. The FIRST event takes one step; the rest of the burst only
//     extends the coalescing window. One flick = one notch, at any velocity.
//   - neither (a detented wheel) — every event is a physical click the user
//     made deliberately, so those still accumulate one notch each and apply
//     together when the spin stops.
//
// ---- ...AND THAT TIMER MUST NOT FIRE UNDER A HELD BUTTON -------------------
// The coalescing timer moved the resize off the wheel event, which also moved
// it out of the user's gesture entirely: it can land while a button is down on
// a COMPLETELY DIFFERENT control. JUCE routes drag events to the component
// that took the mouseDown without consulting hitTest, and juce::Slider's
// RotaryVerticalDrag measures mouseDragStartPos against the LIVE layout, so
// rescaling the canvas mid-drag walks the master knob by several per cent with
// no cursor motion at all. 140 ms is only the floor: on a macOS trackpad the
// inertial tail of a flick keeps being delivered to the original wheel target
// (juce_MouseInputSourceImpl.h routes momentum to lastNonInertialWheelTarget,
// wherever the pointer has since gone), and every momentum event restarts the
// timer — so the commit can land a second or more after the flick, by which
// time the user is plausibly holding something else. The timer therefore
// DEFERS, one tick at a time, while any mouse button is down anywhere. It does
// not drop the step: the spin was deliberate, the readout keeps showing it as
// pending, and it applies the moment the button comes up.
//
// ---- WHAT THE DEFERRAL DOES *NOT* FIX --------------------------------------
// Deferring the commit stops the loop inside one gesture. It does nothing
// about the moment AFTER the commit: the transform is applied synchronously,
// so the whole canvas re-maps under a cursor that has not moved, and this
// slider — 200 px wide, hard against the right edge of an 832 px canvas — has
// the largest displacement of any control on the bar. The very next input
// event therefore lands on whatever slid into that spot: at 100 % -> 125 % the
// master gain knob is one wheel notch away, and the RAW toggle is the second
// click of a double-click away. A UI-scale gesture must never actuate an audio
// parameter, so the class is closed one level up, in the editor: see the
// settle shield in PluginEditor.h. Everything here is the slider's half of
// that contract — `wheelStep` exists so the shield can hand a stray notch back
// to its rightful owner.
//
// This class deliberately records NOTHING about where the pointer was. It used
// to keep a "last gesture aim", and the editor armed its guard from it — which
// is wrong twice: a commit deferred behind the timer above can land seconds
// and hundreds of pixels after the event that produced it, and a keyboard
// commit has no pointer at all. The editor now reads the LIVE pointer at the
// moment it applies the transform, so there is no captured position here to go
// stale.
class PixelScaleSlider final : public juce::Component,
                               public juce::SettableTooltipClient,
                               private juce::Timer
{
public:
    static constexpr int kNumSteps = 5;
    static constexpr int kStepPercent[kNumSteps] = { 100, 125, 150, 175, 200 };

    static constexpr int kNotchPitch = 32;
    static constexpr int kHandleW    = 16;
    static constexpr int kTrackW     = kNotchPitch * (kNumSteps - 1) + kHandleW;  // 144
    static constexpr int kReadoutW   = 48;
    static constexpr int width       = kTrackW + 8 + kReadoutW;                   // 200
    static constexpr int height      = 28;

    // Wheel coalescing window: long enough to swallow one trackpad flick,
    // short enough that a deliberate single notch still feels instant.
    static constexpr int kWheelCommitMs = 140;

    PixelScaleSlider();

    std::function<void (int)> onScaleChange;      // percent

    void setScalePercent (int percent);           // display only, no callback
    int getScalePercent() const noexcept { return kStepPercent[index]; }

    // What the slider currently SHOWS: the pending step while a drag or a
    // wheel spin is in flight, the committed step otherwise.
    int getDisplayedScalePercent() const noexcept
    {
        return kStepPercent[displayIndex()];
    }
    bool hasPendingScale() const noexcept { return pending >= 0; }

    // Apply the pending step now. Called by the wheel timer, by mouseUp, and
    // by the component tests; a no-op when nothing is pending.
    void commitPendingScale();

    // One wheel notch from a gesture with these properties (see "ONE GESTURE,
    // ONE STEP" above; the flags are juce::MouseWheelDetails::isSmooth and
    // ::isInertial verbatim). Public because the editor's settle shield routes
    // notches here that the pointer no longer geometrically covers — the
    // canvas moved out from under it, the user's intent did not — and a routed
    // notch has to carry the kind of gesture it came from, or a routed
    // momentum tail would step again.
    void wheelStep (bool up, bool smoothGesture, bool inertialTail);

    // True while a wheel gesture has taken its step and is only being kept
    // open by further events of the same spin.
    bool hasSteppedThisWheelGesture() const noexcept { return wheelGestureStepped; }

    // True while the mouse button that started a track gesture is still down.
    bool isDraggingScale() const noexcept { return dragging; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void timerCallback() override;
    static int indexForPercent (int percent) noexcept;
    static int indexForMouseX (int x) noexcept;
    int displayIndex() const noexcept { return pending >= 0 ? pending : index; }
    juce::Rectangle<int> trackBounds() const;
    void setIndex (int newIndex, bool notify);
    void setPending (int newIndex);
    void abandonPending();
    // `dragging` gates the wheel and the arrow keys, so a mouseUp that never
    // arrives (host steals the grab, the window closes under the cursor) would
    // leave both dead for the rest of the session. Every entry point that
    // reads the flag re-checks it against the real button state first.
    void forgetLostDrag();

    int index = 0;
    int pending = -1;                             // -1 = nothing in flight
    bool dragging = false;
    // Set by the first step of a continuous wheel gesture; cleared whenever
    // that gesture ends (commit, abandon, authoritative sync, a new grab).
    bool wheelGestureStepped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PixelScaleSlider)
};

// 16x16 star toggle: procedural pixel art, no LookAndFeel button chrome.
// Lit = the current preset is a favorite. Keyboard-reachable like every
// other top-bar control.
class StarButton final : public juce::Button
{
public:
    StarButton() : juce::Button ("FAVORITE") { setWantsKeyboardFocus (true); }
    void setLit (bool shouldBeLit)
    {
        if (shouldBeLit != lit) { lit = shouldBeLit; repaint(); }
    }
    bool isLit() const { return lit; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool lit = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StarButton)
};

class TopBar final : public juce::Component
{
public:
    explicit TopBar (BlockwaveAudioProcessor& processor);

    std::function<void()> onBrowse, onSave, onPresetChanged;
    std::function<void()> onFavoriteToggled;                 // browser count changed
    std::function<void (int)> onScaleChange;                 // percent: 100..200

    void refresh();                                          // preset name/cat
    void setScalePercent (int currentScalePercent);          // display only

    // The one control that must stay live while the canvas settles after a
    // scale change, and the one the settle shield hands stray wheel notches
    // back to (PluginEditor.h).
    PixelScaleSlider& getScaleControl() noexcept { return scaleSlider; }

    // Component IDs of the bar's controls. The first six actuate an audio
    // parameter or move the loaded preset; a gesture aimed at the scale
    // control must never reach one of them (the acceptance property the
    // settle shield exists to hold, asserted in tests/UiComponentTests.h).
    // "namebox" and "led" are also read by PixelLookAndFeel — do not rename.
    static constexpr const char* kIdMaster = "master";
    static constexpr const char* kIdRaw    = "led";
    static constexpr const char* kIdSave   = "preset_save";
    static constexpr const char* kIdFav    = "preset_fav";
    static constexpr const char* kIdPrev   = "preset_prev";
    static constexpr const char* kIdNext   = "preset_next";
    static constexpr const char* kIdName   = "namebox";
    static constexpr const char* kIdBrowse = "preset_browse";
    static constexpr const char* kIdScale  = "uiscale";

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void stepPreset (bool forward);

    BlockwaveAudioProcessor& proc;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, nameBtn,
                     browseBtn { "PRESETS" }, saveBtn { "SAVE" };
    juce::ToggleButton rawBtn { "RAW" };
    StarButton favBtn;
    PixelSlider masterKnob { juce::Slider::RotaryVerticalDrag,
                             juce::Slider::NoTextBox };
    PixelScaleSlider scaleSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> rawAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};

} // namespace blockwave::ui
