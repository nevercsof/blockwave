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

// Component-level UI tests. These drive the REAL juce::Component entry points
// (mouseDown / mouseDrag / mouseUp / mouseWheelMove / keyPressed) on real
// component instances built headlessly, so an interaction contract that used
// to be verifiable only by eye in a checkpoint screenshot is now verifiable by
// the suite. Included by tests/StateTests.cpp, which owns the
// ScopedJuceInitialiser_GUI these components need.
//
//  - PixelScaleSlider: the scale it applies is DEFERRED to mouse-up. A drag
//    must fire nothing (the slider lives inside the component it rescales —
//    applying mid-drag moves the slider under the cursor and the window
//    oscillates), a completed drag must fire exactly once, a drag that ends
//    where it started must fire nothing at all, and a wheel spin must coalesce
//    into a single apply. Arrow keys are inert while the button is down, and a
//    right-press is a context gesture, not a value change.
//  - THE SETTLE PROPERTY, driven through a real editor: no gesture aimed at
//    the scale control may ever actuate master_gain, raw, SAVE, the favourite
//    star or preset navigation. The commit re-maps the whole canvas under a
//    stationary cursor, so this is asserted by resolving the SAME screen point
//    through juce::Component::getComponentAt before and after the commit —
//    the exact routing JUCE itself would use for the next click or notch.
//    The guard is keyed on CONTROL IDENTITY (which control is under the
//    pointer), so the cases that matter are the ones its two predecessors —
//    a 350 ms window and an 8 px re-aim radius — could not reach:
//      * a double-click whose halves are 2 s apart, and a 6 s stationary
//        pause, asserted with REAL elapsed time and the message loop running
//        (letTimePass below), because a guard on a clock only fails when the
//        clock can run;
//      * a 9 px hand drift that never leaves the master knob, and a 10 px
//        twitch that never leaves RAW — the movements that beat a radius;
//      * a commit deferred behind a held button, which lands at a pointer
//        position the requesting event knew nothing about;
//      * a keyboard commit, which has no event position at all.
//    Both directions are asserted throughout: nothing actuates while the
//    pointer is still on the control that slid under it, everything actuates
//    the moment the pointer is on something else — and a commit that changes
//    nothing under the pointer arms no guard at all.
//
//    A headless run has no cursor: no synthetic juce::MouseEvent updates
//    juce::Desktop's mouse source, which is where the editor reads the live
//    pointer. setVirtualPointer parks one, and is the ONLY test-only seam in
//    the whole guard.
//  - CraftGridComponent / CraftCell: the hidden per-block MIX. Hit-testing of
//    the MIX label against the block's drag zone and the knob's own zone,
//    click-to-toggle, the M key, and the rule that the expanded state is pure
//    UI state that dies with the block.
//  - PresetBrowser: the search field. Case-insensitivity, bank-wide matching
//    that overrides the selected folder, the folder-click override in the
//    other direction, and ESC's two-stage clear-then-close.

#include "PluginEditor.h"
#include "ui/TopBar.h"
#include "ui/CraftBlocks.h"
#include "ui/PresetBrowser.h"

namespace uicomponents
{

using namespace blockwave;
using namespace testutil;

// ---------------------------------------------------------------------------
// A MouseEvent aimed at `c`, in `c`'s local coordinates. Same object JUCE
// hands a component, so the handlers under test take their real path.
inline juce::MouseEvent mouseAt (juce::Component& c, juce::Point<int> pos,
                                 juce::Point<int> downPos,
                                 juce::ModifierKeys mods = {})
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                             pos.toFloat(), mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, juce::Time::getCurrentTime(),
                             downPos.toFloat(), juce::Time::getCurrentTime(),
                             1, false);
}

// The scale slider re-checks `dragging` against the REAL button state before
// trusting it (a mouse-up can go missing), so a headless "the button is held"
// has to say so through the same channel JUCE uses.
struct ScopedButtonDown
{
    ScopedButtonDown()
        : saved (juce::ModifierKeys::currentModifiers)
    {
        juce::ModifierKeys::currentModifiers =
            juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
    }
    ~ScopedButtonDown() { juce::ModifierKeys::currentModifiers = saved; }

    juce::ModifierKeys saved;
};

inline juce::MouseWheelDetails wheel (float deltaY)
{
    juce::MouseWheelDetails w {};
    w.deltaX = 0.0f;
    w.deltaY = deltaY;
    w.isReversed = false;
    w.isSmooth = false;                 // a DETENTED wheel: one click, one notch
    w.isInertial = false;
    return w;
}

// One event of a trackpad two-finger scroll: precise deltas, fingers still
// down. macOS reports dozens of these for one gentle flick.
inline juce::MouseWheelDetails wheelSmooth (float deltaY)
{
    auto w = wheel (deltaY);
    w.isSmooth = true;
    return w;
}

// One event of the momentum tail after the fingers lift. JUCE keeps routing
// these to the component that took the FIRST event of the spin, wherever the
// pointer has since travelled (juce_MouseInputSourceImpl.h).
inline juce::MouseWheelDetails wheelInertial (float deltaY)
{
    auto w = wheelSmooth (deltaY);
    w.isInertial = true;
    return w;
}

// ---------------------------------------------------------------------------
static void test_scale_slider_defers_to_mouse_up()
{
    std::printf ("[ui_scale_slider_deferral]\n");
    using Slider = blockwave::ui::PixelScaleSlider;

    Slider s;
    s.setSize (Slider::width, Slider::height);

    int calls = 0, last = 0;
    s.onScaleChange = [&calls, &last] (int p) { ++calls; last = p; };

    CHECK_MSG (s.getScalePercent() == 100, "slider did not start at 100 %%");

    // ---- a drag applies NOTHING until the button comes up -----------------
    const juce::Point<int> down (8, 14);
    s.mouseDown (mouseAt (s, down, down));
    for (const int x : { 20, 40, 70, 100, 40, 104 })
    {
        s.mouseDrag (mouseAt (s, { x, 14 }, down));
        CHECK_MSG (calls == 0,
                   "onScaleChange fired MID-DRAG at x=%d (%d calls) — this is the "
                   "window-oscillation loop", x, calls);
    }
    // ...but the control still shows the user what they are choosing.
    CHECK_MSG (s.hasPendingScale(), "a drag left no pending step to show");
    CHECK_MSG (s.getDisplayedScalePercent() == 175,
               "the handle/readout did not follow the cursor (shows %d %%)",
               s.getDisplayedScalePercent());
    CHECK_MSG (s.getScalePercent() == 100,
               "the committed scale moved during the drag (%d %%)",
               s.getScalePercent());

    s.mouseUp (mouseAt (s, { 104, 14 }, down));
    CHECK_MSG (calls == 1, "mouse-up fired %d callbacks, expected exactly 1", calls);
    CHECK_MSG (last == 175, "mouse-up applied %d %%, expected 175 %%", last);
    CHECK_MSG (s.getScalePercent() == 175, "the committed scale is %d %%",
               s.getScalePercent());
    CHECK_MSG (! s.hasPendingScale(), "a pending step survived mouse-up");

    // ---- a round trip that ends where it started applies nothing ----------
    calls = 0;
    s.mouseDown (mouseAt (s, { 104, 14 }, { 104, 14 }));
    s.mouseDrag (mouseAt (s, { 40, 14 },  { 104, 14 }));
    CHECK_MSG (s.getDisplayedScalePercent() == 125,
               "mid-drag preview wrong on the return trip (%d %%)",
               s.getDisplayedScalePercent());
    s.mouseDrag (mouseAt (s, { 104, 14 }, { 104, 14 }));
    s.mouseUp   (mouseAt (s, { 104, 14 }, { 104, 14 }));
    CHECK_MSG (calls == 0, "a no-op drag fired %d callbacks", calls);
    CHECK_MSG (s.getScalePercent() == 175, "a no-op drag changed the scale to %d %%",
               s.getScalePercent());

    // ---- pressing the readout chip is not a drag at all -------------------
    calls = 0;
    const juce::Point<int> onChip (Slider::kTrackW + 20, 14);
    s.mouseDown (mouseAt (s, onChip, onChip));
    s.mouseDrag (mouseAt (s, { 8, 14 }, onChip));
    s.mouseUp   (mouseAt (s, { 8, 14 }, onChip));
    CHECK_MSG (calls == 0, "dragging off the readout chip fired %d callbacks", calls);
    CHECK_MSG (s.getScalePercent() == 175, "the readout chip moved the scale");

    // ---- a wheel spin coalesces into ONE apply ----------------------------
    calls = 0;
    for (int i = 0; i < 4; ++i)                     // one trackpad flick
        s.mouseWheelMove (mouseAt (s, { 40, 14 }, { 40, 14 }), wheel (-0.25f));
    CHECK_MSG (calls == 0, "a wheel spin fired %d callbacks before settling", calls);
    CHECK_MSG (s.getDisplayedScalePercent() == 100,
               "the wheel preview did not walk down four notches (%d %%)",
               s.getDisplayedScalePercent());
    s.commitPendingScale();                         // what the timer does
    CHECK_MSG (calls == 1, "the settled wheel spin fired %d callbacks", calls);
    CHECK_MSG (last == 100, "the wheel spin applied %d %%", last);

    // ---- arrow keys are single discrete steps and apply immediately -------
    calls = 0;
    s.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    CHECK_MSG (calls == 1 && last == 125, "RIGHT gave %d calls, last %d %%",
               calls, last);
    s.keyPressed (juce::KeyPress (juce::KeyPress::endKey));
    CHECK_MSG (calls == 2 && last == 200, "END gave %d calls, last %d %%",
               calls, last);
    s.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));   // already at the top
    CHECK_MSG (calls == 2, "RIGHT at the last notch fired again (%d calls)", calls);

    // ---- an authoritative display sync drops anything in flight -----------
    calls = 0;
    s.mouseDown (mouseAt (s, { 8, 14 }, { 8, 14 }));
    s.mouseDrag (mouseAt (s, { 8, 14 }, { 8, 14 }));
    s.setScalePercent (150);
    CHECK_MSG (! s.hasPendingScale(), "setScalePercent left a pending step alive");
    s.mouseUp (mouseAt (s, { 8, 14 }, { 8, 14 }));
    CHECK_MSG (calls == 0, "an abandoned drag still applied (%d calls)", calls);
    CHECK_MSG (s.getScalePercent() == 150, "display sync did not stick (%d %%)",
               s.getScalePercent());

    // ---- a key step is INERT while the track gesture is in flight ---------
    // mouseDown grabs the keyboard, so during a drag this slider owns the
    // arrows. Applying one would rescale the canvas mid-gesture and move the
    // slider under the still-held cursor — the oscillation loop the deferral
    // exists to kill, re-entered through the other input device. Auto-repeat
    // makes it a loop, not a one-off.
    calls = 0;
    {
        ScopedButtonDown held;
        s.mouseDown (mouseAt (s, { 8, 14 }, { 8, 14 }));
        s.mouseDrag (mouseAt (s, { 40, 14 }, { 8, 14 }));
        CHECK_MSG (s.isDraggingScale(), "the track press did not start a drag");
        for (const auto code : { juce::KeyPress::rightKey, juce::KeyPress::leftKey,
                                 juce::KeyPress::homeKey, juce::KeyPress::endKey })
        {
            CHECK_MSG (s.keyPressed (juce::KeyPress (code)),
                       "a step key leaked out of the slider mid-drag");
            CHECK_MSG (calls == 0,
                       "a key step fired MID-DRAG (%d calls) — the drag is still "
                       "live and the slider just moved under the cursor", calls);
        }
        CHECK_MSG (s.getDisplayedScalePercent() == 125,
                   "a key step overwrote the drag preview (%d %%)",
                   s.getDisplayedScalePercent());
        s.mouseUp (mouseAt (s, { 40, 14 }, { 8, 14 }));
    }
    CHECK_MSG (calls == 1 && last == 125,
               "the drag did not commit its own step once the keys were ignored "
               "(%d calls, %d %%)", calls, last);

    // ---- a right-press is a context gesture, never a value change ---------
    calls = 0;
    const juce::ModifierKeys rightBtn (juce::ModifierKeys::rightButtonModifier);
    s.mouseDown (mouseAt (s, { 104, 14 }, { 104, 14 }, rightBtn));
    CHECK_MSG (! s.isDraggingScale(), "a right-press started a scale drag");
    s.mouseUp (mouseAt (s, { 104, 14 }, { 104, 14 }, rightBtn));
    CHECK_MSG (calls == 0, "a right-click on the track resized the window (%d calls)",
               calls);
    CHECK_MSG (s.getScalePercent() == 125, "a right-click moved the scale to %d %%",
               s.getScalePercent());

    // ---- a mouse-up that never arrives must not kill the wheel ------------
    // `dragging` gates the wheel and the keys; if the host steals the grab
    // there is no release to clear it, so the flag is re-checked against the
    // real button state instead of being trusted for the rest of the session.
    {
        ScopedButtonDown held;
        s.mouseDown (mouseAt (s, { 8, 14 }, { 8, 14 }));
        CHECK_MSG (s.isDraggingScale(), "the press did not start a drag");
    }                                   // button up, but no mouseUp delivered
    calls = 0;
    s.mouseWheelMove (mouseAt (s, { 40, 14 }, { 40, 14 }), wheel (0.25f));
    CHECK_MSG (! s.isDraggingScale(), "a lost mouse-up left the drag flag stuck");
    s.commitPendingScale();
    CHECK_MSG (calls == 1 && last == 150,
               "the wheel was dead after a lost mouse-up (%d calls, %d %%)",
               calls, last);

    std::printf ("  no callback during drag, exactly one on mouse-up; keys inert "
                 "mid-drag\n");
}

// ---------------------------------------------------------------------------
// THE ACCEPTANCE PROPERTY (adversarial review, targets 1-3):
//
//     no gesture aimed at the scale control may ever actuate master_gain,
//     raw, SAVE, the favourite star, or preset navigation.
//
// This cannot be asserted on a component in isolation, because the defect is
// not in any one component: it is in what a canvas-wide re-map does to event
// ROUTING. A scale commit applies content.setTransform() synchronously, so the
// same SCREEN point maps to a different logical point one instruction later,
// and the scale slider — furthest right on the bar — has the largest
// displacement of anything on it.
//
// juce::Component::getComponentAt walks exactly the hit-test + inverse-
// transform chain JUCE uses to deliver the next click or wheel notch, so
// resolving the review's own screen coordinates through a REAL editor before
// and after a commit is the defect reproduced at the level it lives at. Each
// case also asserts its CANARY: with the guard lifted, that point really does
// resolve to the dangerous control. If a future layout change makes it stop
// doing so, the test says "re-aim me" instead of passing hollowly.
static const char* const kForbiddenIds[] = {
    blockwave::ui::TopBar::kIdMaster, blockwave::ui::TopBar::kIdRaw,
    blockwave::ui::TopBar::kIdSave,   blockwave::ui::TopBar::kIdFav,
    blockwave::ui::TopBar::kIdPrev,   blockwave::ui::TopBar::kIdNext,
    blockwave::ui::TopBar::kIdName,   blockwave::ui::TopBar::kIdBrowse
};

// The id of the parameter/navigation control this point would actuate, or an
// empty string if it would actuate none. Walks up the chain so an inner part
// of a composite control still counts as its owner.
static juce::String actuatorIdAt (juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        for (const auto* id : kForbiddenIds)
            if (c->getComponentID() == id)
                return c->getComponentID();
    return {};
}

// One press+release delivered to whatever JUCE would deliver it to.
static void clickThroughEditor (BlockwaveAudioProcessorEditor& ed,
                                juce::Point<int> screenPt)
{
    if (auto* target = ed.getComponentAt (screenPt))
    {
        const auto p = target->getLocalPoint (&ed, screenPt);
        target->mouseDown (mouseAt (*target, p, p));
        target->mouseUp   (mouseAt (*target, p, p));
    }
}

// One wheel notch delivered to whatever JUCE would deliver it to. Same routing
// as a click: the shield is transparent unless the notch is a stale aim, so
// this is also how "the notch reached the component underneath" is asserted.
static void wheelThroughEditor (BlockwaveAudioProcessorEditor& ed,
                                juce::Point<int> screenPt, float deltaY)
{
    if (auto* target = ed.getComponentAt (screenPt))
    {
        const auto p = target->getLocalPoint (&ed, screenPt);
        target->mouseWheelMove (mouseAt (*target, p, p), wheel (deltaY));
    }
}

// REAL elapsed time, with the message loop dispatching. The settle guard is
// keyed on pointer movement and not on a clock, and the only honest way to
// assert that is to run the clock: juce::Timer callbacks (the slider's 140 ms
// wheel coalescer, the shield's backstop) only fire while messages are being
// pumped, so a bare sleep would prove nothing — under the old time-keyed guard
// this is exactly what let the window expire. The state-test target defines
// JUCE_MODAL_LOOPS_PERMITTED for this.
static void letTimePass (int ms)
{
    auto* mm = juce::MessageManager::getInstance();
    const auto deadline = juce::Time::getMillisecondCounter()
                            + static_cast<juce::uint32> (juce::jmax (0, ms));
    for (auto now = juce::Time::getMillisecondCounter(); now < deadline;
         now = juce::Time::getMillisecondCounter())
        mm->runDispatchLoopUntil (static_cast<int> (deadline - now));
}

static void test_scale_gesture_never_reaches_a_parameter (BlockwaveAudioProcessor& proc)
{
    std::printf ("[ui_scale_settle_shield]\n");

    // Every commit below writes the machine-wide scale store; redirect it so a
    // test run never touches the user's ~/Documents/BLOCKWAVE/Settings.json.
    const auto tempSettings =
        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("blockwave-scale-settle-test.json");
    tempSettings.deleteFile();
    blockwave::GlobalSettings::setDefaultFile (tempSettings);

    {
        BlockwaveAudioProcessorEditor ed (proc);
        ed.setVisible (true);                      // getComponentAt needs it
        ed.setUiScale (100, false);
        ed.endScaleSettle();

        auto* masterParam = proc.apvts.getRawParameterValue ("master_gain");
        auto* rawParam    = proc.apvts.getRawParameterValue ("raw");
        CHECK_MSG (masterParam != nullptr && rawParam != nullptr,
                   "master_gain / raw parameters missing");

        // The three screen points from the review, in the editor's own
        // coordinate space (= physical pixels of the plugin window).
        const juce::Point<int> wheelPt (715, 20);      // track -> master @125 %
        const juce::Point<int> dblPt   (660, 25);      // track -> RAW    @125 %
        const juce::Point<int> savePt  (760, 28);      // track -> SAVE   @200 %

        auto* scale = dynamic_cast<blockwave::ui::PixelScaleSlider*> (
                          ed.getComponentAt (wheelPt));
        CHECK_MSG (scale != nullptr,
                   "screen %d,%d is not the scale track at 100 %% — the repro "
                   "coordinates no longer describe the bar", wheelPt.x, wheelPt.y);

        if (masterParam != nullptr && rawParam != nullptr && scale != nullptr)
        {
            const float master0 = masterParam->load();
            const float raw0    = rawParam->load();
            const int   preset0 = proc.getPresetLibrary().getCurrentIndex();

            // ---- 1) WHEEL: a SLOW discrete spin, notches 600 ms apart -----
            // 600 ms is past every fixed window this guard has ever had (the
            // 140 ms coalescer plus the old 350 ms shield = 490 ms) and past
            // the macOS double-click interval as well. The pointer never
            // moves, so every one of these notches still belongs to the scale
            // control however long the user takes between them.
            ed.setVirtualPointer (wheelPt);            // the cursor is parked here
            const auto trackLocal = scale->getLocalPoint (&ed, wheelPt);
            scale->mouseWheelMove (mouseAt (*scale, trackLocal, trackLocal),
                                   wheel (0.5f));
            letTimePass (600);                         // the REAL 140 ms timer
            CHECK_MSG (ed.getUiScale() == 125, "the first notch applied %d %%",
                       ed.getUiScale());
            CHECK_MSG (ed.isSettlingAfterScaleChange(),
                       "the commit did not arm the settle shield");

            for (const int expected : { 150, 175 })
            {
                CHECK_MSG (ed.isSettlingAfterScaleChange(),
                           "the shield was down 600 ms after a commit with the "
                           "cursor stationary — the guard is keyed on a clock "
                           "again, and the next notch is on master_gain");
                const auto wheelHit = actuatorIdAt (ed.getComponentAt (wheelPt));
                CHECK_MSG (wheelHit.isEmpty(),
                           "the next wheel notch would be delivered to \"%s\" — a "
                           "UI scale gesture is one notch from changing the sound",
                           wheelHit.toRawUTF8());

                // ...and the notch is not merely dropped: it is handed back to
                // the control the gesture was aimed at, so a slow spin keeps
                // stepping even though the track has slid out from under the
                // pointer (at 125 % it starts at screen x 780).
                wheelThroughEditor (ed, wheelPt, 0.5f);
                letTimePass (600);
                CHECK_MSG (ed.getUiScale() == expected,
                           "a notch %d ms after the last one did not reach the "
                           "scale control (%d %%, expected %d %%)", 600,
                           ed.getUiScale(), expected);
                CHECK_MSG (masterParam->load() == master0,
                           "a slow wheel spin moved master_gain (%.4f -> %.4f)",
                           master0, masterParam->load());
            }

            // Canary: with the guard lifted this point IS the master knob.
            ed.endScaleSettle();
            CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                       "endScaleSettle left the shield up");
            ed.setUiScale (125, false);
            const auto bareWheelHit = actuatorIdAt (ed.getComponentAt (wheelPt));
            CHECK_MSG (bareWheelHit == blockwave::ui::TopBar::kIdMaster,
                       "screen %d,%d at 125 %% no longer resolves to the master "
                       "knob (\"%s\") — re-aim this test", wheelPt.x, wheelPt.y,
                       bareWheelHit.toRawUTF8());

            // ---- 2) DOUBLE-CLICK: the second press, at ANY interval -------
            // macOS's double-click interval is 500 ms by default and goes to
            // ~1 s with the Accessibility slider, so a guard that expires on a
            // clock is a guard the OS can walk straight past. The cursor is
            // stationary for the whole of each gap, which is what a
            // double-click IS.
            for (const int gapMs : { 400, 800, 2000 })
            {
                ed.setUiScale (100, false);
                ed.endScaleSettle();
                CHECK_MSG (ed.getComponentAt (dblPt) == scale,
                           "screen %d,%d is not the scale track at 100 %%",
                           dblPt.x, dblPt.y);
                ed.setVirtualPointer (dblPt);
                const auto dblLocal = scale->getLocalPoint (&ed, dblPt);
                scale->mouseDown (mouseAt (*scale, dblLocal, dblLocal));
                scale->mouseUp   (mouseAt (*scale, dblLocal, dblLocal));
                CHECK_MSG (ed.getUiScale() == 125, "the first click applied %d %%",
                           ed.getUiScale());

                letTimePass (gapMs);

                CHECK_MSG (ed.isSettlingAfterScaleChange(),
                           "the shield expired %d ms after the commit with the "
                           "cursor stationary — no fixed window can be right "
                           "here, the OS decides how slow a double-click is",
                           gapMs);
                const auto dblHit = actuatorIdAt (ed.getComponentAt (dblPt));
                CHECK_MSG (dblHit.isEmpty(),
                           "the second press of a double-click %d ms later would "
                           "be delivered to \"%s\"", gapMs, dblHit.toRawUTF8());
                clickThroughEditor (ed, dblPt);
                CHECK_MSG (rawParam->load() == raw0,
                           "the stray second click %d ms later toggled RAW "
                           "(%.1f -> %.1f)", gapMs, raw0, rawParam->load());
                CHECK_MSG (ed.getUiScale() == 125,
                           "the stray second click resized the window again (%d %%)",
                           ed.getUiScale());
            }

            ed.endScaleSettle();
            const auto bareDblHit = actuatorIdAt (ed.getComponentAt (dblPt));
            CHECK_MSG (bareDblHit == blockwave::ui::TopBar::kIdRaw,
                       "screen %d,%d at 125 %% no longer resolves to RAW (\"%s\") "
                       "— re-aim this test", dblPt.x, dblPt.y,
                       bareDblHit.toRawUTF8());

            // ---- 3) the same at the other end of the track ----------------
            ed.setUiScale (100, false);
            ed.endScaleSettle();
            ed.setVirtualPointer (savePt);
            const auto saveLocal = scale->getLocalPoint (&ed, savePt);
            scale->mouseDown (mouseAt (*scale, saveLocal, saveLocal));
            scale->mouseUp   (mouseAt (*scale, saveLocal, saveLocal));
            CHECK_MSG (ed.getUiScale() == 200, "the click applied %d %%",
                       ed.getUiScale());
            letTimePass (800);                         // long past any window
            CHECK_MSG (ed.isSettlingAfterScaleChange(),
                       "the shield expired 800 ms after the 200 %% commit");
            const auto saveHit = actuatorIdAt (ed.getComponentAt (savePt));
            CHECK_MSG (saveHit.isEmpty(),
                       "the second click of a double-click would hit \"%s\"",
                       saveHit.toRawUTF8());
            clickThroughEditor (ed, savePt);
            CHECK_MSG (! ed.isSavePanelVisible(),
                       "the stray second click opened the save panel");

            ed.endScaleSettle();
            const auto bareSaveHit = actuatorIdAt (ed.getComponentAt (savePt));
            CHECK_MSG (bareSaveHit == blockwave::ui::TopBar::kIdSave,
                       "screen %d,%d at 200 %% no longer resolves to SAVE (\"%s\") "
                       "— re-aim this test", savePt.x, savePt.y,
                       bareSaveHit.toRawUTF8());

            // ---- 4) drag, then an arrow key, with the button still down ---
            ed.setUiScale (100, false);
            ed.endScaleSettle();
            {
                ScopedButtonDown held;
                const juce::Point<int> downLocal (8, 14);
                scale->mouseDown (mouseAt (*scale, downLocal, downLocal));
                scale->mouseDrag (mouseAt (*scale, { 40, 14 }, downLocal));
                scale->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
                scale->keyPressed (juce::KeyPress (juce::KeyPress::endKey));
                CHECK_MSG (ed.getUiScale() == 100,
                           "an arrow key resized the canvas mid-drag (%d %%) — the "
                           "drag is still live and now reads a moved slider",
                           ed.getUiScale());
                scale->mouseUp (mouseAt (*scale, { 40, 14 }, downLocal));
            }
            CHECK_MSG (ed.getUiScale() == 125,
                       "the drag did not commit its own step (%d %%)",
                       ed.getUiScale());

            // ---- 5) a pending wheel commit must not fire under a held button
            // The 140 ms coalescer moved the resize out of the user's gesture:
            // it can land while a button is down on a DIFFERENT control, and
            // JUCE routes drags to the grab target without consulting hitTest,
            // so no shield can help. juce::Slider measures its drag against the
            // live layout, so a resize under a held knob walks the value with
            // no cursor motion at all. On a trackpad the inertial tail keeps
            // restarting this timer, so the commit can be seconds late — which
            // is why the timer asks "is a button down NOW", not "how long ago
            // was the spin".
            ed.setUiScale (100, false);
            ed.endScaleSettle();
            ed.setVirtualPointer (wheelPt);
            {
                ScopedButtonDown held;             // ...on the master knob
                const auto tl = scale->getLocalPoint (&ed, wheelPt);
                scale->mouseWheelMove (mouseAt (*scale, tl, tl), wheel (0.5f));
                CHECK_MSG (scale->hasPendingScale(),
                           "the notch left no pending step to defer");
                letTimePass (600);                 // >> the 140 ms commit timer
                CHECK_MSG (ed.getUiScale() == 100,
                           "a wheel commit resized the canvas under a held button "
                           "(%d %%) — whatever that button is dragging now reads "
                           "a layout that moved", ed.getUiScale());
                CHECK_MSG (scale->hasPendingScale(),
                           "the deferred step was dropped, not deferred — the "
                           "user's spin was deliberate and must still apply");
                CHECK_MSG (masterParam->load() == master0,
                           "master_gain moved while the resize was deferred "
                           "(%.4f -> %.4f)", master0, masterParam->load());
            }
            letTimePass (400);                     // button up: it lands now
            CHECK_MSG (ed.getUiScale() == 125,
                       "the deferred step never applied after the button came up "
                       "(%d %%)", ed.getUiScale());

            // ---- 6) the shield is not a global scale control --------------
            // hitTest gates CLICKS geometrically; the wheel must be gated the
            // same way or an armed shield turns every notch on the canvas into
            // a scale step. The pointer is on the preset list, nowhere near the
            // stale aim point, so the notch is not stale and must go through.
            ed.setUiScale (100, false);
            ed.endScaleSettle();
            ed.showPresetBrowser (true);
            auto& br = ed.getBrowser();
            const juce::Point<int> listPt (400, 300);

            // Canary: with no shield up, this point scrolls the list. The
            // browser opens parked on the loaded preset, which can be at
            // either end of the pane, so probe toward the end that has room.
            const int scroll0 = br.getListScrollY();
            const float probeDelta = scroll0 > 0 ? 1.0f : -1.0f;
            wheelThroughEditor (ed, listPt, probeDelta);
            const int scrolled = br.getListScrollY();
            CHECK_MSG (scrolled != scroll0,
                       "screen %d,%d does not scroll the preset list even with no "
                       "shield up (%d) — re-aim this test", listPt.x, listPt.y,
                       scrolled);
            wheelThroughEditor (ed, listPt, -probeDelta);
            CHECK_MSG (br.getListScrollY() == scroll0, "the list did not scroll back");

            ed.setVirtualPointer (wheelPt);
            const auto tl2 = scale->getLocalPoint (&ed, wheelPt);
            scale->mouseWheelMove (mouseAt (*scale, tl2, tl2), wheel (0.5f));
            letTimePass (400);
            CHECK_MSG (ed.getUiScale() == 125 && ed.isSettlingAfterScaleChange(),
                       "the browser-open commit did not arm the shield (%d %%)",
                       ed.getUiScale());
            wheelThroughEditor (ed, listPt, probeDelta);
            CHECK_MSG (br.getListScrollY() == scrolled,
                       "a notch over the preset list did not reach it while the "
                       "shield was armed over the top bar (%d, expected %d)",
                       br.getListScrollY(), scrolled);
            CHECK_MSG (ed.getUiScale() == 125,
                       "a notch over the preset list stepped the UI scale (%d %%) "
                       "— the shield became a canvas-wide scale control",
                       ed.getUiScale());
            wheelThroughEditor (ed, listPt, -probeDelta);
            ed.showPresetBrowser (false);
            ed.endScaleSettle();

            // ---- 7) ...and it IS a window: re-aiming ends it at once -------
            ed.setUiScale (100, false);
            ed.endScaleSettle();
            ed.setVirtualPointer (savePt);
            const auto saveLocal2 = scale->getLocalPoint (&ed, savePt);
            scale->mouseDown (mouseAt (*scale, saveLocal2, saveLocal2));
            scale->mouseUp   (mouseAt (*scale, saveLocal2, saveLocal2));
            CHECK_MSG (ed.getUiScale() == 200 && ed.isSettlingAfterScaleChange(),
                       "the 200 %% commit did not arm the shield (%d %%)",
                       ed.getUiScale());
            CHECK_MSG (ed.getSettleAnchorId() == blockwave::ui::TopBar::kIdSave,
                       "the shield anchored to \"%s\", not to the control that "
                       "slid under the pointer",
                       ed.getSettleAnchorId().toRawUTF8());

            // A tremor is not a re-aim.
            ed.notePointerMoved (savePt.translated (3, 0));
            CHECK_MSG (ed.isSettlingAfterScaleChange(),
                       "a 3 px twitch counted as a re-aim — the second press of "
                       "a double-click never lands on the exact same pixel");

            // ...and NEITHER IS 40 px. This assertion used to read the other
            // way round, and it was the review's own exhibit for why a
            // distance threshold cannot work: SAVE at 200 % is screen
            // 752..848 x 24..56, so 40 px of "re-aim" is still squarely on
            // SAVE, and the very next line of the old test asserted exactly
            // that. A guard that a movement can drop while the hazard is
            // still under the cursor is not a guard.
            ed.notePointerMoved (savePt.translated (40, 0));
            CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (
                           savePt.translated (40, 0)))
                           == blockwave::ui::TopBar::kIdSave,
                       "40 px from %d,%d is no longer inside SAVE — this case no "
                       "longer demonstrates what it is here to demonstrate",
                       savePt.x, savePt.y);
            CHECK_MSG (ed.isSettlingAfterScaleChange(),
                       "a 40 px drift dropped the shield while SAVE was still "
                       "directly under the cursor — that is the distance-keyed "
                       "hole, reopened");

            // LEAVING THE CONTROL is the re-aim, and it ends the settle
            // immediately — no waiting on any clock, which is also why this is
            // not sticky. One pixel past SAVE's bottom edge is enough.
            const auto offSave = juce::Point<int> (savePt.x, 58);
            CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (offSave)).isEmpty(),
                       "screen %d,%d is still on an actuator — re-aim this case",
                       offSave.x, offSave.y);
            ed.notePointerMoved (offSave);
            CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                       "the shield survived the pointer leaving the control it "
                       "was anchored to — a guard that only ever goes up is not "
                       "a guard");
            CHECK_MSG (! ed.isWatchingPointerForSettle(),
                       "the disarmed shield kept its global mouse listener");
            const auto reaimedHit = actuatorIdAt (ed.getComponentAt (savePt));
            CHECK_MSG (reaimedHit == blockwave::ui::TopBar::kIdSave,
                       "after the re-aim this point resolves to \"%s\", not SAVE",
                       reaimedHit.toRawUTF8());
            CHECK_MSG (! ed.isSavePanelVisible(), "the save panel was already open");
            clickThroughEditor (ed, savePt);
            CHECK_MSG (ed.isSavePanelVisible(),
                       "SAVE was still dead after the user re-aimed — the guard "
                       "is a permanent dead zone, not a settle window");
            ed.showSavePanel (false);

            // ---- 8) THE PROPERTY, over the whole sequence -----------------
            CHECK_MSG (masterParam->load() == master0,
                       "a UI-scale gesture moved master_gain (%.4f -> %.4f)",
                       master0, masterParam->load());
            CHECK_MSG (rawParam->load() == raw0,
                       "a UI-scale gesture moved raw (%.1f -> %.1f)",
                       raw0, rawParam->load());
            CHECK_MSG (proc.getPresetLibrary().getCurrentIndex() == preset0,
                       "a UI-scale gesture moved the preset selection (%d -> %d)",
                       preset0, proc.getPresetLibrary().getCurrentIndex());
            CHECK_MSG (! ed.getBrowser().isVisible(),
                       "a UI-scale gesture opened the preset browser");
            CHECK_MSG (! ed.isSavePanelVisible(),
                       "a UI-scale gesture left the save panel open");
        }

        ed.setUiScale (100, false);
        ed.endScaleSettle();
    }

    blockwave::GlobalSettings::setDefaultFile ({});
    tempSettings.deleteFile();

    std::printf ("  slow wheel / 2 s double-click / held button / list scroll: "
                 "0 parameter actuations, guard drops on re-aim\n");
}

// ---------------------------------------------------------------------------
// THE IDENTITY PROPERTY (third adversarial review, findings 1-5).
//
// The property above says a scale gesture must not actuate a parameter. This
// one says WHY the guard holds: it is keyed on which control is under the
// pointer, so it has no threshold for a movement or a pause to walk past.
// Every case here is a repro from the review that its predecessors failed:
//
//   1  a 9 px drift that never leaves the master knob (beat the 8 px radius);
//   2  a 10 px twitch that never leaves RAW (same);
//   3  the pointer actually LEAVING the control — the positive case, without
//      which the guard would be indistinguishable from always-on;
//   4  6 s of dispatched time with a stationary cursor (beat the 5 s backstop
//      that no longer exists);
//   5  a commit deferred behind a held button, landing 143 px from the wheel
//      event that asked for it (beat the captured aim);
//   6  a keyboard commit with a parked cursor (was exempt from the guard
//      entirely, and the exemption was device-specific for no reason);
//   7  a commit that changes nothing under the pointer, which must arm
//      nothing at all;
//   8  the production disarm path, which had no observable and could be
//      deleted with the whole suite still green.
static void test_scale_settle_is_keyed_on_control_identity (BlockwaveAudioProcessor& proc)
{
    std::printf ("[ui_scale_settle_identity]\n");

    const auto tempSettings =
        juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("blockwave-scale-identity-test.json");
    tempSettings.deleteFile();
    blockwave::GlobalSettings::setDefaultFile (tempSettings);

    {
        BlockwaveAudioProcessorEditor ed (proc);
        ed.setVisible (true);
        ed.setUiScale (100, false);
        ed.endScaleSettle();

        auto& scale = ed.getTopBar().getScaleControl();
        auto* masterParam = proc.apvts.getRawParameterValue ("master_gain");
        auto* rawParam    = proc.apvts.getRawParameterValue ("raw");
        CHECK_MSG (masterParam != nullptr && rawParam != nullptr,
                   "master_gain / raw parameters missing");
        if (masterParam == nullptr || rawParam == nullptr)
            return;

        const juce::Point<int> wheelPt (715, 20);   // scale track  @100 %
        const juce::Point<int> dblPt   (660, 25);   // scale track  @100 %
        const juce::Point<int> knobPt  (572, 25);   // master knob  @100 %
        const juce::Point<int> chipPt  (800, 20);   // readout chip @100 %

        // One notch of a DETENTED wheel delivered wherever JUCE would deliver
        // it, with the cursor parked at that point, then long enough for the
        // 140 ms coalescer to commit.
        auto spinAt = [&ed] (juce::Point<int> p, float delta)
        {
            ed.setVirtualPointer (p);
            wheelThroughEditor (ed, p, delta);
            letTimePass (200);
        };
        auto reset = [&ed] { ed.setUiScale (100, false); ed.endScaleSettle(); };

        // ---- 1) A 9 PX DRIFT INSIDE THE MASTER KNOB --------------------
        // masterKnob at 125 % is screen 700..729 x 10..39. An 8 px radius
        // around (715,20) sits entirely inside it, so EVERY movement that
        // dropped the old guard left the pointer on the hazard.
        reset();
        const float master0 = masterParam->load();
        spinAt (wheelPt, 0.5f);
        CHECK_MSG (ed.getUiScale() == 125, "the notch applied %d %%", ed.getUiScale());
        CHECK_MSG (ed.getSettleAnchorId() == blockwave::ui::TopBar::kIdMaster,
                   "the shield anchored to \"%s\", not to the master knob that "
                   "slid under the cursor", ed.getSettleAnchorId().toRawUTF8());

        const auto drift = wheelPt.translated (9, 0);
        CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (drift))
                       == blockwave::ui::TopBar::kIdMaster,
                   "9 px from %d,%d is no longer the master knob — this case no "
                   "longer demonstrates what it is here to demonstrate",
                   wheelPt.x, wheelPt.y);
        ed.setVirtualPointer (drift);
        CHECK_MSG (ed.isSettlingAfterScaleChange(),
                   "a 9 px hand drift dropped the guard while the master knob "
                   "was still directly under the cursor — the distance-keyed "
                   "hole, reopened");
        const auto driftHit = actuatorIdAt (ed.getComponentAt (drift));
        CHECK_MSG (driftHit.isEmpty(),
                   "after a 9 px drift the next notch goes to \"%s\"",
                   driftHit.toRawUTF8());

        // ...and it is not merely dropped: the gesture still owns the wheel.
        wheelThroughEditor (ed, drift, 0.5f);
        letTimePass (200);
        CHECK_MSG (ed.getUiScale() == 150,
                   "the notch after the drift did not reach the scale control "
                   "(%d %%)", ed.getUiScale());
        CHECK_MSG (masterParam->load() == master0,
                   "a 9 px drift let a scale gesture move master_gain "
                   "(%.4f -> %.4f)", master0, masterParam->load());

        // ---- 2) A 10 PX TWITCH INSIDE RAW ------------------------------
        // rawBtn at 125 % is screen 610..679 x 15..34; (660,25) -> (670,25)
        // is 10 px and never leaves it.
        reset();
        const float raw0 = rawParam->load();
        ed.setVirtualPointer (dblPt);
        {
            const auto local = scale.getLocalPoint (&ed, dblPt);
            scale.mouseDown (mouseAt (scale, local, local));
            scale.mouseUp   (mouseAt (scale, local, local));
        }
        CHECK_MSG (ed.getUiScale() == 125, "the click applied %d %%", ed.getUiScale());
        CHECK_MSG (ed.getSettleAnchorId() == blockwave::ui::TopBar::kIdRaw,
                   "the shield anchored to \"%s\", not to RAW",
                   ed.getSettleAnchorId().toRawUTF8());

        const auto twitch = dblPt.translated (10, 0);
        CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (twitch))
                       == blockwave::ui::TopBar::kIdRaw,
                   "10 px from %d,%d is no longer RAW — re-aim this case",
                   dblPt.x, dblPt.y);
        ed.setVirtualPointer (twitch);
        CHECK_MSG (ed.isSettlingAfterScaleChange(),
                   "a 10 px twitch dropped the guard with RAW still under the "
                   "cursor");
        clickThroughEditor (ed, twitch);
        CHECK_MSG (rawParam->load() == raw0,
                   "the second press of a double-click, 10 px on, toggled RAW "
                   "(%.1f -> %.1f)", raw0, rawParam->load());

        // ---- 3) THE POSITIVE CASE: leaving the control ends it ---------
        // Without this the guard would be indistinguishable from always-on.
        const juce::Point<int> offRaw (twitch.x, 45);
        CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (offRaw)).isEmpty(),
                   "screen %d,%d is still on an actuator — re-aim this case",
                   offRaw.x, offRaw.y);
        ed.setVirtualPointer (offRaw);
        CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                   "the shield survived the pointer leaving RAW");
        CHECK_MSG (actuatorIdAt (ed.getComponentAt (twitch))
                       == blockwave::ui::TopBar::kIdRaw,
                   "RAW is still unreachable after the re-aim");
        clickThroughEditor (ed, twitch);
        CHECK_MSG (rawParam->load() != raw0,
                   "RAW was still dead after the user re-aimed — the guard is a "
                   "permanent dead zone, not a settle");
        clickThroughEditor (ed, twitch);            // put the parameter back
        CHECK_MSG (rawParam->load() == raw0, "the test did not restore raw");

        // ---- 4) SIX SECONDS OF DISPATCHED TIME -------------------------
        // No clock is consulted anywhere in the guard, and the 5 s backstop
        // that used to end a settle on its own is gone. "Step it, sit back
        // and look, step it again" is ordinary, and a >5 s look used to
        // reopen findings 1 and 2 verbatim.
        reset();
        spinAt (wheelPt, 0.5f);
        CHECK_MSG (ed.isSettlingAfterScaleChange(), "the notch armed nothing");
        letTimePass (6000);
        CHECK_MSG (ed.isSettlingAfterScaleChange(),
                   "the shield died after 6 s with the cursor stationary — a "
                   "backstop is back, and it dies with the cursor still stale");
        CHECK_MSG (ed.getSettleAnchorId() == blockwave::ui::TopBar::kIdMaster,
                   "6 s on, the shield is anchored to \"%s\"",
                   ed.getSettleAnchorId().toRawUTF8());
        const auto lateHit = actuatorIdAt (ed.getComponentAt (wheelPt));
        CHECK_MSG (lateHit.isEmpty(),
                   "6 s after the commit the next notch goes to \"%s\"",
                   lateHit.toRawUTF8());
        wheelThroughEditor (ed, wheelPt, 0.5f);
        letTimePass (200);
        CHECK_MSG (ed.getUiScale() == 150,
                   "the notch 6 s later did not reach the scale control (%d %%)",
                   ed.getUiScale());
        CHECK_MSG (masterParam->load() == master0,
                   "a 6 s pause let a scale gesture move master_gain "
                   "(%.4f -> %.4f)", master0, masterParam->load());

        // ---- 5) A DEFERRED COMMIT ARMS AT THE LIVE POINTER -------------
        // Four notches on the track, then press-and-hold the master knob
        // 143 px away (the first click of a double-click-to-reset). The
        // coalescer defers the commit under the held button, so the
        // transform lands where the pointer IS, not where the wheel was.
        reset();
        const int preset0 = proc.getPresetLibrary().getCurrentIndex();
        ed.setVirtualPointer (wheelPt);
        {
            const auto local = scale.getLocalPoint (&ed, wheelPt);
            for (int i = 0; i < 4; ++i)
                scale.mouseWheelMove (mouseAt (scale, local, local), wheel (0.5f));
        }
        CHECK_MSG (scale.getDisplayedScalePercent() == 200,
                   "four detented notches previewed %d %%",
                   scale.getDisplayedScalePercent());
        {
            ScopedButtonDown held;
            ed.setVirtualPointer (knobPt);          // the press is HERE
            letTimePass (400);
            CHECK_MSG (ed.getUiScale() == 100,
                       "the commit landed under a held button (%d %%)",
                       ed.getUiScale());
        }
        letTimePass (400);                          // released: it lands now
        CHECK_MSG (ed.getUiScale() == 200,
                   "the deferred step never applied (%d %%)", ed.getUiScale());
        CHECK_MSG (ed.getSettleAnchorId() == blockwave::ui::TopBar::kIdNext,
                   "the shield anchored to \"%s\" — a deferred commit must arm "
                   "at the pointer's REAL position, not at the wheel's",
                   ed.getSettleAnchorId().toRawUTF8());
        const auto deferredHit = actuatorIdAt (ed.getComponentAt (knobPt));
        CHECK_MSG (deferredHit.isEmpty(),
                   "the second press of the double-click would hit \"%s\"",
                   deferredHit.toRawUTF8());
        clickThroughEditor (ed, knobPt);
        CHECK_MSG (proc.getPresetLibrary().getCurrentIndex() == preset0,
                   "the second press of a double-click changed the preset "
                   "(%d -> %d)", preset0, proc.getPresetLibrary().getCurrentIndex());

        // Canary: with the guard lifted, that point really is the NEXT arrow.
        ed.endScaleSettle();
        const auto bareNext = actuatorIdAt (ed.getComponentAt (knobPt));
        CHECK_MSG (bareNext == blockwave::ui::TopBar::kIdNext,
                   "screen %d,%d at 200 %% no longer resolves to the next-preset "
                   "arrow (\"%s\") — re-aim this case", knobPt.x, knobPt.y,
                   bareNext.toRawUTF8());

        // ---- 6) A KEYBOARD COMMIT IS GUARDED THE SAME WAY --------------
        // Click the readout chip (focus, no drag), then step with the arrows.
        // The invariant is about what is under the cursor, so the device that
        // asked for the step is irrelevant — the old keyboard exemption left
        // RAW parked under a stationary cursor with no guard at all.
        reset();
        ed.setVirtualPointer (chipPt);
        {
            const auto local = scale.getLocalPoint (&ed, chipPt);
            scale.mouseDown (mouseAt (scale, local, local));
            scale.mouseUp   (mouseAt (scale, local, local));
        }
        CHECK_MSG (ed.getUiScale() == 100,
                   "pressing the readout chip resized the window (%d %%)",
                   ed.getUiScale());

        scale.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        CHECK_MSG (ed.getUiScale() == 125, "RIGHT applied %d %%", ed.getUiScale());
        // ---- 7) NOTHING MOVED UNDER THE POINTER -> NO GUARD ------------
        // The track is still under the cursor at 125 %, so there is nothing
        // stale and the guard must not exist.
        CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                   "a commit that left the scale control under the pointer armed "
                   "a shield anyway (anchored to \"%s\")",
                   ed.getSettleAnchorId().toRawUTF8());
        CHECK_MSG (! ed.isWatchingPointerForSettle(),
                   "...and it registered a global mouse listener for it");

        scale.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        CHECK_MSG (ed.getUiScale() == 150, "RIGHT applied %d %%", ed.getUiScale());
        CHECK_MSG (ed.isSettlingAfterScaleChange(),
                   "a keyboard commit slid RAW under a parked cursor and armed "
                   "nothing — the guard must not be device-specific");
        CHECK_MSG (ed.getSettleAnchorId() == blockwave::ui::TopBar::kIdRaw,
                   "the keyboard commit anchored to \"%s\", not RAW",
                   ed.getSettleAnchorId().toRawUTF8());
        const float rawBeforeKeys = rawParam->load();
        clickThroughEditor (ed, chipPt);
        CHECK_MSG (rawParam->load() == rawBeforeKeys,
                   "a click after two arrow presses toggled RAW (%.1f -> %.1f)",
                   rawBeforeKeys, rawParam->load());
        ed.endScaleSettle();
        const auto bareKeyHit = actuatorIdAt (ed.getComponentAt (chipPt));
        CHECK_MSG (bareKeyHit == blockwave::ui::TopBar::kIdRaw,
                   "screen %d,%d at 150 %% no longer resolves to RAW (\"%s\") — "
                   "re-aim this case", chipPt.x, chipPt.y, bareKeyHit.toRawUTF8());

        // ...and the same with the pointer, so 7) is not a keyboard property.
        reset();
        spinAt (chipPt, 0.5f);
        CHECK_MSG (ed.getUiScale() == 125, "the notch applied %d %%", ed.getUiScale());
        CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                   "a wheel commit that left the scale control under the pointer "
                   "armed a shield (anchored to \"%s\")",
                   ed.getSettleAnchorId().toRawUTF8());

        // ...and with something that is NOT the scale control under the
        // pointer, so 7) does not rest on the scale-control exemption either.
        // A bare patch of the top bar that is still a bare patch of the top
        // bar one step later has moved nothing under the cursor, so a step
        // there must leave no residue at all — otherwise every commit would
        // drop a dead spot somewhere on the canvas.
        reset();
        const juce::Point<int> gapPt (600, 20);
        CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (gapPt)).isEmpty(),
                   "screen %d,%d at 100 %% is on an actuator — re-aim this case",
                   gapPt.x, gapPt.y);
        ed.setVirtualPointer (gapPt);
        scale.keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        CHECK_MSG (ed.getUiScale() == 125, "RIGHT applied %d %%", ed.getUiScale());
        CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (gapPt)).isEmpty(),
                   "screen %d,%d at 125 %% now resolves to \"%s\" — re-aim this "
                   "case, it is supposed to change nothing", gapPt.x, gapPt.y,
                   actuatorIdAt (ed.controlUnderPointer (gapPt)).toRawUTF8());
        CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                   "a commit that changed nothing under the pointer armed a "
                   "shield anyway (anchored to \"%s\")",
                   ed.getSettleAnchorId().toRawUTF8());
        CHECK_MSG (! ed.isWatchingPointerForSettle(),
                   "...and registered a global mouse listener for it");

        // ---- 9) THE SCALE CONTROL IS NEVER THE THING SHIELDED ----------
        // Stepping DOWN can slide the scale control itself under a pointer
        // that was on bare top bar. Shielding it would freeze the control the
        // user is actively working — the sticky failure — and it is not a
        // hazard: it is neither an audio parameter nor preset navigation.
        ed.setUiScale (150, false);
        ed.endScaleSettle();
        const juce::Point<int> barGapPt (900, 20);
        CHECK_MSG (actuatorIdAt (ed.controlUnderPointer (barGapPt)).isEmpty(),
                   "screen %d,%d at 150 %% is on an actuator — re-aim this case",
                   barGapPt.x, barGapPt.y);
        ed.setVirtualPointer (barGapPt);
        scale.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        CHECK_MSG (ed.getUiScale() == 125, "LEFT applied %d %%", ed.getUiScale());
        CHECK_MSG (ed.controlUnderPointer (barGapPt) == &scale,
                   "screen %d,%d at 125 %% is not the scale control — re-aim "
                   "this case", barGapPt.x, barGapPt.y);
        CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                   "the scale control slid under the pointer and the guard "
                   "shielded it (anchored to \"%s\") — that freezes the control "
                   "the user is working", ed.getSettleAnchorId().toRawUTF8());
        CHECK_MSG (ed.getComponentAt (barGapPt) == &scale,
                   "the scale control is unreachable at screen %d,%d right "
                   "after it slid under the pointer", barGapPt.x, barGapPt.y);

        // ---- 8) THE PRODUCTION DISARM PATH IS OBSERVABLE ---------------
        // isWatchingPointerForSettle() is the presence of the juce::Desktop
        // registration object itself, not a bool kept beside it: delete the
        // registration and this goes false, which is the only thing that
        // brings a real shield down on a real machine.
        reset();
        CHECK_MSG (! ed.isWatchingPointerForSettle(),
                   "a disarmed editor is watching the pointer");
        spinAt (wheelPt, 0.5f);
        CHECK_MSG (ed.isSettlingAfterScaleChange(),
                   "the notch armed nothing to watch for");
        CHECK_MSG (ed.isWatchingPointerForSettle(),
                   "the shield went up without registering its juce::Desktop "
                   "mouse listener — on a real machine nothing would ever bring "
                   "it down");
        ed.notePointerMoved (juce::Point<int> (wheelPt.x, 300));
        CHECK_MSG (! ed.isSettlingAfterScaleChange(),
                   "the listener's own entry point did not disarm the shield");
        CHECK_MSG (! ed.isWatchingPointerForSettle(),
                   "the disarmed shield kept its listener registered");

        CHECK_MSG (masterParam->load() == master0,
                   "the whole sequence moved master_gain (%.4f -> %.4f)",
                   master0, masterParam->load());
        CHECK_MSG (rawParam->load() == raw0,
                   "the whole sequence moved raw (%.1f -> %.1f)", raw0,
                   rawParam->load());

        reset();
    }

    blockwave::GlobalSettings::setDefaultFile ({});
    tempSettings.deleteFile();

    std::printf ("  9 px drift / 10 px twitch / 6 s pause / deferred commit / "
                 "arrow keys: guarded by identity, dropped on leaving\n");
}

// ---------------------------------------------------------------------------
// Finding 6: one gesture, one step. A macOS trackpad reports a gentle
// two-finger scroll as a burst of precise-delta events and then a long
// momentum tail, all delivered to whatever took the first event. Counting
// events counted the hardware's sampling rate, so one flick used to saturate
// the pending index and take the window from 100 % straight to 200 %.
static void test_wheel_gesture_is_one_step()
{
    std::printf ("[ui_scale_wheel_one_gesture_one_step]\n");
    using Slider = blockwave::ui::PixelScaleSlider;

    Slider s;
    s.setSize (Slider::width, Slider::height);
    int calls = 0, last = 0;
    s.onScaleChange = [&calls, &last] (int p) { ++calls; last = p; };

    const juce::Point<int> on (40, 14);
    auto flick = [&s, &on] (float delta, int moving, int coasting)
    {
        for (int i = 0; i < moving; ++i)
            s.mouseWheelMove (mouseAt (s, on, on), wheelSmooth (delta));
        for (int i = 0; i < coasting; ++i)
            s.mouseWheelMove (mouseAt (s, on, on), wheelInertial (delta * 0.4f));
    };

    // ---- one flick = one notch, whatever the event count ------------------
    flick (0.25f, 12, 20);
    CHECK_MSG (calls == 0, "the flick applied %d times before settling", calls);
    CHECK_MSG (s.getDisplayedScalePercent() == 125,
               "one gentle two-finger flick previewed %d %% — 32 events became "
               "%d steps", s.getDisplayedScalePercent(),
               (s.getDisplayedScalePercent() - 100) / 25);
    s.commitPendingScale();
    CHECK_MSG (calls == 1 && last == 125, "the flick applied %d times, last %d %%",
               calls, last);

    // ...and the control is not dead afterwards: a second flick is a second
    // notch.
    flick (0.25f, 9, 14);
    s.commitPendingScale();
    CHECK_MSG (calls == 2 && last == 150, "the second flick gave %d calls, %d %%",
               calls, last);

    // ---- momentum alone is never a step -----------------------------------
    // The tail keeps arriving after the commit; if it could start a gesture,
    // one flick would step twice.
    calls = 0;
    for (int i = 0; i < 20; ++i)
        s.mouseWheelMove (mouseAt (s, on, on), wheelInertial (0.10f));
    CHECK_MSG (! s.hasPendingScale(),
               "the momentum tail started a step of its own (%d %%)",
               s.getDisplayedScalePercent());
    s.commitPendingScale();
    CHECK_MSG (calls == 0 && s.getScalePercent() == 150,
               "momentum after the commit moved the scale (%d calls, %d %%)",
               calls, s.getScalePercent());

    // ---- a DETENTED wheel still accumulates -------------------------------
    // There every event is a click the user physically made, so two clicks
    // are two notches — clamping those to one would be the opposite defect.
    calls = 0;
    for (int i = 0; i < 2; ++i)
        s.mouseWheelMove (mouseAt (s, on, on), wheel (-0.5f));
    CHECK_MSG (s.getDisplayedScalePercent() == 100,
               "two detented clicks down from 150 %% previewed %d %%",
               s.getDisplayedScalePercent());
    s.commitPendingScale();
    CHECK_MSG (calls == 1 && last == 100, "%d calls, last %d %%", calls, last);

    std::printf ("  32-event flick = 1 notch, momentum = 0, 2 detents = 2 notches\n");
}

// ---------------------------------------------------------------------------
// Bench cell geometry (CraftBlocks.h): 64x64, MIX label 16x12 top-right, knob
// hit box 28x28 bottom-right.
namespace cellpoints
{
    inline juce::Point<int> label()  { return { 56, 5 }; }    // on the letters
    inline juce::Point<int> face()   { return { 20, 20 }; }   // plain block face
    inline juce::Point<int> knob()   { return { 50, 50 }; }   // knob centre-ish
    inline juce::Point<int> corner() { return { 6, 6 }; }     // top-left: drag zone
}

static blockwave::ui::CraftCell* cellOf (blockwave::ui::CraftGridComponent& g,
                                         int slot)
{
    for (int i = 0; i < g.getNumChildComponents(); ++i)
        if (auto* c = dynamic_cast<blockwave::ui::CraftCell*> (g.getChildComponent (i)))
            if (! c->isBase() && c->getSlot() == slot)
                return c;
    return nullptr;
}

static void clickCell (blockwave::ui::CraftCell& c, juce::Point<int> p)
{
    c.mouseDown (mouseAt (c, p, p));
    c.mouseUp   (mouseAt (c, p, p));
}

static void test_craft_hidden_mix_control()
{
    std::printf ("[ui_craft_hidden_mix]\n");
    blockwave::ui::BlockImageCache cache;
    blockwave::ui::CraftGridComponent grid (cache);
    grid.setSize (blockwave::ui::kCraftGridW, blockwave::ui::kCraftGridW);

    Material ice {};
    CHECK_MSG (materialFromName ("ICE", ice), "ICE material missing");
    grid.placeMaterial (0, ice);

    auto* cell = cellOf (grid, 0);
    CHECK_MSG (cell != nullptr, "bench cell 0 not found");
    if (cell == nullptr)
        return;

    // ---- default: closed. A block never arrives with its knob open --------
    CHECK_MSG (! grid.isMixKnobOpen (0), "a freshly placed block opened its knob");

    // ---- clicking the MIX label toggles, clicking the face does not -------
    clickCell (*cell, cellpoints::face());
    CHECK_MSG (! grid.isMixKnobOpen (0), "clicking the block face opened the knob");

    clickCell (*cell, cellpoints::label());
    CHECK_MSG (grid.isMixKnobOpen (0), "clicking the MIX label did not open the knob");
    clickCell (*cell, cellpoints::label());
    CHECK_MSG (! grid.isMixKnobOpen (0), "clicking the MIX label again did not hide it");

    // ---- a press on the label that TRAVELS is a block move, not a toggle --
    cell->mouseDown (mouseAt (*cell, cellpoints::label(), cellpoints::label()));
    cell->mouseDrag (mouseAt (*cell, { 20, 40 }, cellpoints::label()));
    cell->mouseUp   (mouseAt (*cell, { 20, 40 }, cellpoints::label()));
    CHECK_MSG (! grid.isMixKnobOpen (0),
               "dragging off the MIX label still toggled the knob");

    // ---- with the knob OPEN the block's drag zone is untouched ------------
    grid.setMixKnobOpen (0, true);
    CHECK_MSG (grid.isMixKnobOpen (0), "setMixKnobOpen did not open the knob");
    const float before = grid.cellWeight (0);
    cell->mouseDown (mouseAt (*cell, cellpoints::corner(), cellpoints::corner()));
    cell->mouseDrag (mouseAt (*cell, { 6, 40 }, cellpoints::corner()));
    cell->mouseUp   (mouseAt (*cell, { 6, 40 }, cellpoints::corner()));
    CHECK_MSG (grid.cellWeight (0) == before,
               "dragging the BLOCK moved the mix (%.2f -> %.2f) — the knob is "
               "eating the drag zone", before, grid.cellWeight (0));

    // ...while a drag inside the knob's own box does move it.
    int weightEdits = 0;
    grid.onCellWeightEdited = [&weightEdits] (int, float) { ++weightEdits; };
    cell->mouseDown (mouseAt (*cell, cellpoints::knob(), cellpoints::knob()));
    cell->mouseDrag (mouseAt (*cell, { 50, 50 + 24 }, cellpoints::knob()));
    cell->mouseUp   (mouseAt (*cell, { 50, 50 + 24 }, cellpoints::knob()));
    CHECK_MSG (std::abs (grid.cellWeight (0) - 0.5f) < 1.0e-4f,
               "a 24 px knob drag gave %.3f, expected 0.50", grid.cellWeight (0));
    CHECK_MSG (weightEdits > 0, "the knob drag never reported a weight edit");

    // ---- the wheel only bites over an OPEN knob ---------------------------
    const float open = grid.cellWeight (0);
    cell->mouseWheelMove (mouseAt (*cell, cellpoints::face(), cellpoints::face()),
                          wheel (0.5f));
    CHECK_MSG (std::abs (grid.cellWeight (0) - (open + 0.05f)) < 1.0e-4f,
               "the wheel did not nudge an open knob by 5 %% (%.3f)",
               grid.cellWeight (0));
    grid.setMixKnobOpen (0, false);
    const float closed = grid.cellWeight (0);
    cell->mouseWheelMove (mouseAt (*cell, cellpoints::face(), cellpoints::face()),
                          wheel (0.5f));
    CHECK_MSG (grid.cellWeight (0) == closed,
               "the wheel edited the mix with no control on screen (%.3f -> %.3f)",
               closed, grid.cellWeight (0));

    // ---- M is the keyboard twin of the label ------------------------------
    const juce::KeyPress mKey ('m', juce::ModifierKeys(), 'm');
    CHECK_MSG (cell->keyPressed (mKey), "M was not consumed by a filled cell");
    CHECK_MSG (grid.isMixKnobOpen (0), "M did not open the knob");
    cell->keyPressed (mKey);
    CHECK_MSG (! grid.isMixKnobOpen (0), "M did not hide the knob again");

    // ---- UP/DOWN belong to the mix only while the knob is open ------------
    grid.setMixKnobOpen (0, true);
    const float beforeArrow = grid.cellWeight (0);
    cell->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
    CHECK_MSG (std::abs (grid.cellWeight (0) - (beforeArrow - 0.05f)) < 1.0e-4f,
               "DOWN did not nudge the open knob (%.3f)", grid.cellWeight (0));

    // ---- expanded state is UI state: it dies with the block ---------------
    grid.clearSlot (0);
    CHECK_MSG (! grid.isMixKnobOpen (0), "clearing the cell left its knob open");
    grid.setMixKnobOpen (0, true);
    CHECK_MSG (! grid.isMixKnobOpen (0), "an EMPTY cell opened a mix knob");

    // ...and a whole-bench replacement closes every knob.
    grid.placeMaterial (2, ice);
    grid.setMixKnobOpen (2, true);
    CraftGrid fresh;
    fresh.cells[2] = ice;
    grid.setGrid (fresh);
    CHECK_MSG (! grid.isMixKnobOpen (2),
               "a preset load restored an expanded knob — that is not preset state");

    // ...and the knob travels with its block across a move.
    grid.setMixKnobOpen (2, true);
    grid.moveMaterial (2, 5);
    CHECK_MSG (grid.isMixKnobOpen (5) && ! grid.isMixKnobOpen (2),
               "the expanded knob did not follow its block across the bench");

    std::printf ("  label toggles, knob owns 28x28, block drag intact\n");
}

// ---------------------------------------------------------------------------
// Defect 5: a preset that carries NO craft data must leave the bench in its
// genuine uncrafted state, not showing the blocks (and open MIX knobs) of
// whatever was there before. Such presets are producible from inside the
// plugin — SAVE on a never-crafted instance writes one, because
// buildCurrentPresetVar omits "craft" when craftValid is false.
static void test_craft_bench_resyncs_to_a_no_craft_preset (BlockwaveAudioProcessor& proc)
{
    std::printf ("[ui_craft_no_craft_resync]\n");
    blockwave::ui::CraftTab craft (proc);
    craft.setBounds (0, 0, blockwave::ui::kCanvasW, blockwave::ui::kContentH);

    Material ice {};
    CHECK_MSG (materialFromName ("ICE", ice), "ICE material missing");

    CraftGrid g;
    g.cells[0] = ice;
    g.cells[3] = ice;
    proc.setCraftGrid (g);
    craft.refreshFromProcessor();
    craft.setMixKnobOpen (0, true);
    CHECK_MSG (craft.isShowingCraftGrid(), "the bench did not take the grid");
    CHECK_MSG (craft.getShownGrid().cells[0] == ice, "slot 0 is not ICE");
    CHECK_MSG (craft.isMixKnobOpen (0), "the MIX knob did not open");

    // Exactly what loading a preset with no "craft" key does to the processor:
    // craftValid goes false and craftGrid is left untouched.
    proc.clearCraftGrid();
    craft.refreshFromProcessor();

    CHECK_MSG (! craft.isShowingCraftGrid(),
               "the bench still claims to hold a craft grid");
    for (int i = 0; i < kNumCells; ++i)
        CHECK_MSG (craft.getShownGrid().cells[i] == Material::none,
                   "slot %d survived a no-craft preset load — the bench is "
                   "misrepresenting the sound that just loaded", i);
    CHECK_MSG (! craft.isMixKnobOpen (0),
               "an open MIX knob survived a no-craft preset load");

    // ...and the 1 s safety net now covers the same case: it compares against
    // the empty grid instead of skipping the comparison when the processor has
    // no craft, so a bench left stale by any other path is picked up too.
    proc.setCraftGrid (g);
    craft.refreshFromProcessor();
    CHECK_MSG (craft.isShowingCraftGrid(), "the bench did not reload the grid");
    proc.clearCraftGrid();
    CraftGrid probe;
    CHECK_MSG (! proc.getCraftGrid (probe),
               "clearCraftGrid left the processor claiming a valid grid");
    CHECK_MSG (! probe.equalsWithWeights (craft.getShownGrid()),
               "the stale-bench comparison the safety net makes cannot fire "
               "here — this test is not exercising the net");
    craft.refreshFromProcessor();
    CHECK_MSG (probe.equalsWithWeights (craft.getShownGrid()),
               "the resync is not stable: the net would refresh forever");

    std::printf ("  no-craft preset empties the bench and closes every knob\n");
}

// ---------------------------------------------------------------------------
static void test_preset_browser_search (BlockwaveAudioProcessor& proc)
{
    std::printf ("[ui_preset_search]\n");
    blockwave::ui::PresetBrowser br (proc);
    br.setBounds (0, 0, blockwave::ui::kCanvasW,
                  blockwave::ui::kCanvasH - blockwave::ui::kTopBarH);

    const auto& lib = proc.getPresetLibrary();
    auto expectedHits = [&lib] (const juce::String& q)
    {
        int n = 0;
        for (int i = 0; i < lib.getNumPresets(); ++i)
            if (lib.getPreset (i).name.containsIgnoreCase (q))
                ++n;
        return n;
    };

    CHECK_MSG (! br.isSearching(), "a fresh browser started with a query");

    // ---- substring, case-insensitive, whole bank --------------------------
    br.setSearchQuery ("st");
    CHECK_MSG (br.isSearching(), "setSearchQuery did not arm the search");
    CHECK_MSG (br.getSearchQuery() == "ST", "the query is not uppercased (%s)",
               br.getSearchQuery().toRawUTF8());
    const int hits = br.getSearchMatchCount();
    CHECK_MSG (hits == expectedHits ("ST"),
               "search \"ST\" listed %d presets, expected %d", hits,
               expectedHits ("ST"));
    CHECK_MSG (hits > 0 && hits < lib.getNumPresets(),
               "search \"ST\" is not actually filtering (%d of %d)", hits,
               lib.getNumPresets());

    br.setSearchQuery ("ST");                    // same query, different case
    CHECK_MSG (br.getSearchMatchCount() == hits, "case changed the match count");

    // ---- the query overrides the selected folder --------------------------
    br.selectFolder (0, "PAD");
    const int padOnly = br.getSearchMatchCount();
    CHECK_MSG (! br.isSearching(), "selecting a folder did not clear the search");
    br.setSearchQuery ("ST");
    CHECK_MSG (br.getSearchMatchCount() == hits,
               "the search stayed inside FACTORY/PAD (%d, expected the bank-wide %d)",
               br.getSearchMatchCount(), hits);
    CHECK_MSG (hits != padOnly || padOnly == 0,
               "the folder shot cannot prove the override (both %d)", hits);

    // ---- ...and touching a folder overrides the query, in both directions -
    br.selectFolder (0, "PAD");
    CHECK_MSG (! br.isSearching(), "a folder selection left the query armed");
    CHECK_MSG (br.getSearchMatchCount() == padOnly,
               "the folder did not take the list back (%d, expected %d)",
               br.getSearchMatchCount(), padOnly);

    // ---- no hits is a real, reachable state -------------------------------
    br.setSearchQuery ("ZQXJV");
    CHECK_MSG (br.getSearchMatchCount() == 0, "a nonsense query matched %d presets",
               br.getSearchMatchCount());

    // ---- ESC clears first, closes second ----------------------------------
    int closes = 0;
    br.onClose = [&closes] { ++closes; };
    br.setSearchQuery ("ST");
    CHECK_MSG (br.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)),
               "ESC was not consumed");
    CHECK_MSG (! br.isSearching(), "ESC did not clear the query");
    CHECK_MSG (closes == 0, "ESC closed the browser instead of clearing (%d)", closes);
    br.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK_MSG (closes == 1, "ESC on an empty field did not close (%d)", closes);

    // ---- the field only eats letters in its own zone ----------------------
    // Straight out of the box the browser is on the FOLDERS zone, so a bare
    // letter must NOT start a query (F still belongs to the star toggle).
    br.clearSearch();
    br.keyPressed (juce::KeyPress ('f', juce::ModifierKeys(), 'f'));
    CHECK_MSG (! br.isSearching(),
               "a bare letter typed into the folder pane started a search — "
               "that would break the F star binding");
    br.focusSearchField();
    br.keyPressed (juce::KeyPress ('f', juce::ModifierKeys(), 'f'));
    CHECK_MSG (br.getSearchQuery() == "F",
               "the SEARCH zone did not accept a letter (%s)",
               br.getSearchQuery().toRawUTF8());
    br.keyPressed (juce::KeyPress (juce::KeyPress::backspaceKey));
    CHECK_MSG (br.getSearchQuery().isEmpty(), "BACKSPACE did not delete");

    // ---- the query is length-clamped --------------------------------------
    br.setSearchQuery (juce::String::repeatedString ("A", 200));
    CHECK_MSG (br.getSearchQuery().length()
                   == blockwave::ui::PresetBrowser::kMaxQueryLength,
               "the query is not clamped (%d chars)", br.getSearchQuery().length());
    br.clearSearch();

    std::printf ("  %d/%d hits for \"ST\", folder override both ways\n",
                 hits, lib.getNumPresets());
}

// ---------------------------------------------------------------------------
inline void runAll (BlockwaveAudioProcessor& proc)
{
    test_scale_slider_defers_to_mouse_up();
    test_wheel_gesture_is_one_step();
    test_scale_gesture_never_reaches_a_parameter (proc);
    test_scale_settle_is_keyed_on_control_identity (proc);
    test_craft_hidden_mix_control();
    test_craft_bench_resyncs_to_a_no_craft_preset (proc);
    test_preset_browser_search (proc);
}

} // namespace uicomponents
