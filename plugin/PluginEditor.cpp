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

#include "PluginEditor.h"

using namespace blockwave::ui;

void BlockwaveAudioProcessorEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (colours::night);
    // Deterministic speckle dither (fixed seed) — subtle depth, still night.
    juce::Random rnd (0x0b10c0);
    g.setColour (colours::speckle);
    for (int i = 0; i < 260; ++i)
    {
        const int x = rnd.nextInt (kCanvasW / 2) * 2;
        const int y = rnd.nextInt (kCanvasH / 2) * 2;
        g.fillRect (x, y, 2, 2);
    }
}

// ---- settle shield -----------------------------------------------------------
// Full rationale in PluginEditor.h. Short version: a scale commit re-maps the
// canvas under a stationary cursor, so an event arriving now would actuate
// whatever slid into that spot rather than what the user was aiming at. The
// shield is that sentence and nothing else — it is anchored to the control
// that slid under the pointer, it is solid exactly where that control would
// receive the event, and it is down the moment the pointer is somewhere else.
// No clock. No radius.

BlockwaveAudioProcessorEditor::SettleShield::SettleShield (
    BlockwaveAudioProcessorEditor& ownerEditor)
    : owner (ownerEditor)
{
    setOpaque (false);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);   // focus must not move either
    setInterceptsMouseClicks (true, false);
    setAlwaysOnTop (false);
}

BlockwaveAudioProcessorEditor::SettleShield::~SettleShield()
{
    // Not disarm(): a dying component must not repaint or change visibility.
    // The one thing that MUST go is the desktop-wide listener, which outlives
    // this object's storage if left attached.
    watch.reset();
}

void BlockwaveAudioProcessorEditor::SettleShield::arm (juce::Component* aimedAt,
                                                       juce::Component* slidUnder,
                                                       bool aimOwnsWheel)
{
    jassert (slidUnder != nullptr && slidUnder != aimedAt);
    anchor = slidUnder;
    aim = aimedAt;
    wheelBelongsToScale = aimOwnsWheel;
    setVisible (true);
    toFront (false);                           // above the overlays, no focus

    // The registration is desktop-wide, so it sees movement anywhere in the
    // process rather than only over the shield — including the events the
    // shield itself swallows. juce::Desktop also starts a 100 ms cursor poll
    // while any global listener exists (juce_Desktop.cpp), which synthesises
    // the mouseMove that would otherwise never arrive when the real cursor has
    // moved without generating an event. It is registered ONLY while a shield
    // is up, and the first tick that finds the pointer off the anchor disarms
    // us, which drops the listener again.
    if (watch == nullptr)
        watch = std::make_unique<GlobalWatch> (pointerWatch);
}

void BlockwaveAudioProcessorEditor::SettleShield::disarm()
{
    watch.reset();
    anchor = nullptr;
    aim = nullptr;
    wheelBelongsToScale = false;
    setVisible (false);
}

void BlockwaveAudioProcessorEditor::SettleShield::notePointer (juce::Point<int> screenPos)
{
    // THE DISARM, and the whole of it. The shield exists for exactly one
    // control; the moment the pointer is not on that control the user has
    // demonstrably re-aimed and there is nothing stale left to protect. No
    // threshold in either direction: a 9 px drift inside the master knob is
    // still the master knob and still stale, a 3 px step off its edge is not.
    if (! isArmed())
        return;
    auto* a = anchor.getComponent();
    if (a == nullptr || owner.controlUnderPointer (screenPos) != a)
        disarm();
}

bool BlockwaveAudioProcessorEditor::SettleShield::hitTest (int x, int y)
{
    // IDENTITY, NOT GEOMETRY AND NOT TIME. Solid exactly where the event would
    // be delivered to the anchor — the control that slid under the pointer —
    // and see-through everywhere else, which is what keeps the preset list
    // scrollable and the tabs clickable while a shield is up, and why the
    // shield cannot become a canvas-wide scale control.
    if (transparentToQueries)
        return false;                          // an identity query, not an event
    auto* a = anchor.getComponent();
    if (a == nullptr)
        return false;
    // (x, y) are already canvas coordinates: the shield spans the canvas 1:1,
    // so this is the same point JUCE would have carried down to the anchor.
    return owner.controlAtCanvasPoint (juce::Point<int> (x, y).toFloat()) == a;
}

void BlockwaveAudioProcessorEditor::SettleShield::mouseWheelMove (
    const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    // Only stale notches are delivered here (see hitTest). GESTURE OWNERSHIP:
    // if the control the user was aiming at is the scale control, this notch
    // still belongs to it — give it back instead of dropping it, so a slow
    // spin keeps stepping the scale one notch at a time, at ANY cadence.
    // Otherwise swallow it: a notch at a stale point must not reach whatever
    // slid under the pointer (over the top bar that is master gain; over the
    // bench it is a mix weight — the sound, either way).
    // Deliberately NOT passed to the parent: the base class implementation
    // forwards up the chain and it would find the master knob again.
    if (w.deltaY != 0.0f && wheelBelongsToScale && onStaleWheel != nullptr)
        onStaleWheel (w.deltaY > 0.0f, w.isSmooth, w.isInertial);
}

// ---- editor ------------------------------------------------------------------

BlockwaveAudioProcessorEditor::BlockwaveAudioProcessorEditor (BlockwaveAudioProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      topBar (p),
      craftTab (p),
      tweakTab (p.apvts),
      browser (p),
      tooltips (&content, 700)
{
    content.setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kCanvasW, kCanvasH);

    content.addAndMakeVisible (topBar);
    topBar.setBounds (0, 0, kCanvasW, kTopBarH);

    for (auto* b : { &craftTabBtn, &tweakTabBtn })
    {
        b->setComponentID ("tab");
        b->setClickingTogglesState (false);
        content.addAndMakeVisible (*b);
    }
    craftTabBtn.setTooltip ("place the blocks");
    tweakTabBtn.setTooltip ("every knob exposed");
    craftTabBtn.setBounds (8, kTopBarH, 96, kTabStripH);
    tweakTabBtn.setBounds (112, kTopBarH, 96, kTabStripH);
    craftTabBtn.onClick = [this] { setActiveTab (Tab::craft); };
    tweakTabBtn.onClick = [this] { setActiveTab (Tab::tweak); };

    content.addAndMakeVisible (craftTab);
    craftTab.setBounds (0, kContentY, kCanvasW, kContentH);
    content.addAndMakeVisible (tweakTab);
    tweakTab.setBounds (0, kContentY, kCanvasW, kContentH);

    content.addChildComponent (browser);
    browser.setBounds (0, kTopBarH, kCanvasW, kCanvasH - kTopBarH);
    content.addChildComponent (savePanel);
    savePanel.setBounds (0, kTopBarH, kCanvasW, kCanvasH - kTopBarH);

    // Added last => frontmost. Hidden until a scale commit arms it.
    content.addChildComponent (settleShield);
    settleShield.setBounds (0, 0, kCanvasW, kCanvasH);
    settleShield.onStaleWheel = [this] (bool up, bool smooth, bool inertial)
    {
        topBar.getScaleControl().wheelStep (up, smooth, inertial);
    };

    // ---- wiring (all message thread) ---------------------------------------
    topBar.onBrowse = [this] { showPresetBrowser (! browser.isVisible()); };
    topBar.onSave = [this] { showSavePanel (true); };
    topBar.onPresetChanged = [this]
    {
        browser.refresh();
        craftTab.refreshFromProcessor();          // preset carries a craft grid
    };
    // The ONLY path that can arm the settle shield. The restore-on-open call
    // and the screenshot tool go straight to setUiScale: no cursor gesture is
    // in flight there and nothing must freeze.
    topBar.onScaleChange = [this] (int s) { commitScaleFromGesture (s); };
    topBar.onFavoriteToggled = [this] { browser.refresh(); };

    browser.onLoad = [this] (int index)
    {
        juce::String err;
        proc.loadPresetAtIndex (index, err);
        topBar.refresh();
        browser.refresh();
        craftTab.refreshFromProcessor();
    };
    browser.onClose = [this] { showPresetBrowser (false); };
    browser.onFavoritesChanged = [this] { topBar.refresh(); };

    savePanel.onSave = [this] (const juce::String& name, const juce::String& category)
    {
        juce::String err;
        if (! proc.saveCurrentAsUserPreset (name, category, err))
        {
            savePanel.setError (err);
            return;
        }
        showSavePanel (false);
        topBar.refresh();
        browser.refresh();
    };
    savePanel.onCancel = [this] { showSavePanel (false); };

    // The discovery toast + pixel flourish fire inside CraftTab; the jingle is
    // synthesized by the processor after its master stage
    // (src/DiscoveryJingle.h). Both lambdas capture the processor by pointer,
    // not the editor: the processor always outlives this component.
    auto* pp = &proc;
    craftTab.onDiscovery = [pp] (const juce::String&) { pp->triggerDiscoveryJingle(); };

    // Keyboard strip -> lock-free processor inbox (src/UiMidiQueue.h). Giving
    // setMidiSink real callbacks is also what makes the strip paint itself as
    // live instead of tagging itself "NO MIDI PATH".
    craftTab.setMidiSink ([pp] (int note, float velocity) { pp->uiNoteOn (note, velocity); },
                          [pp] (int note) { pp->uiNoteOff (note); });

    setActiveTab (Tab::craft);                        // CRAFT is home (SPEC)

    // Scale precedence: session (this project) -> global (this machine) ->
    // 100 %. hasProperty, not a getProperty default, is what tells a project
    // that genuinely stored 100 % apart from one that never stored anything.
    // GlobalSettings::sanitiseScale also migrates the legacy 1x/2x integers
    // sessions saved before the percent scheme.
    int storedScale = 0;
    if (proc.apvts.state.hasProperty ("uiScale"))
        storedScale = blockwave::GlobalSettings::sanitiseScale (
            static_cast<int> (proc.apvts.state.getProperty ("uiScale")));
    if (storedScale == 0)
        storedScale = settings.getUiScalePercent();
    if (storedScale == 0)
        storedScale = 100;
    setUiScale (storedScale, false);                  // restore: no global write
}

BlockwaveAudioProcessorEditor::~BlockwaveAudioProcessorEditor()
{
    // Drop the shield's global mouse listener before anything starts dying:
    // its disarm check and its wheel route both reach members of this editor.
    endScaleSettle();
    // The editor can be destroyed with a key still down (host closes the
    // window mid-click). Release everything the UI is holding before the key
    // strip goes away; the processor releases only ITS notes, so anything the
    // host is playing keeps sounding.
    proc.uiAllNotesOff();
    content.setLookAndFeel (nullptr);
}

juce::Point<int> BlockwaveAudioProcessorEditor::pointerScreenPos() const
{
    // Fresh, every time. NOT a position captured at the event that asked for
    // the step: the wheel coalescer can defer a commit behind a held button
    // for as long as that button is down, so an aim captured at the notch can
    // be seconds and hundreds of pixels out of date by the time the transform
    // lands — and a keyboard commit has no event position at all.
    if (virtualPointer.has_value())
        return *virtualPointer;
    return juce::Desktop::getInstance().getMainMouseSource()
             .getScreenPosition().roundToInt();
}

juce::Component* BlockwaveAudioProcessorEditor::controlAtCanvasPoint (
    juce::Point<float> canvasPt)
{
    // juce::Component::getComponentAt is the same hit-test + inverse-transform
    // walk JUCE uses to deliver the next click or notch, so this is the
    // routing itself rather than a model of it. The shield is excluded for the
    // duration: it must never hide the answer it is being asked about.
    const SettleShield::ScopedTransparent hide (settleShield);
    return content.getComponentAt (canvasPt);
}

juce::Component* BlockwaveAudioProcessorEditor::controlUnderPointer (
    juce::Point<int> screenPos)
{
    return controlAtCanvasPoint (content.getLocalPoint (nullptr, screenPos.toFloat()));
}

bool BlockwaveAudioProcessorEditor::isScaleControl (juce::Component* c)
{
    auto* scaleCtl = &topBar.getScaleControl();
    for (; c != nullptr; c = c->getParentComponent())
        if (c == scaleCtl)
            return true;
    return false;
}

void BlockwaveAudioProcessorEditor::commitScaleFromGesture (int scalePercent)
{
    // ---- THE GUARD. Three lines of it, and no threshold in any of them ----
    const auto pointer = pointerScreenPos();
    auto* before = controlUnderPointer (pointer);

    // The aim is the control the user last DELIBERATELY looked at. Normally
    // that is simply what is under the pointer now — but if a shield is still
    // up and the pointer is still on its anchor, the pointer has not moved
    // since the previous commit, so the aim from that commit is still the
    // user's. Inheriting it is what a chained wheel spin is: without it, the
    // second notch would "aim" at whatever the FIRST commit slid under the
    // cursor, ownership would answer "not the scale control", and the spin
    // would die at its second notch.
    auto* inheritedAim = settleShield.getAim();
    auto* aim = (settleShield.isArmed() && before == settleShield.getAnchor()
                   && inheritedAim != nullptr)
                    ? inheritedAim : before;
    const bool aimOwnsWheel = isScaleControl (aim);

    settleShield.disarm();          // clean slate; re-armed below if warranted
    setUiScale (scalePercent);      // <- the canvas re-maps HERE

    auto* after = controlUnderPointer (pointer);

    // NOTHING MOVED UNDER THE POINTER -> NOTHING IS STALE -> NO SHIELD. This
    // is the common case for the small steps and it is why the guard cannot be
    // sticky: it does not exist unless the canvas actually put something else
    // where the user was aiming.
    if (after == nullptr || after == aim)
        return;

    // ...and the scale control is never shielded. If IT is what slid under the
    // pointer the user can just keep stepping, which is strictly better than
    // freezing the control they are using; and it is not a hazard, being
    // neither an audio parameter nor preset navigation.
    if (isScaleControl (after))
        return;

    settleShield.arm (aim, after, aimOwnsWheel);
}

void BlockwaveAudioProcessorEditor::endScaleSettle()
{
    settleShield.disarm();
}

juce::String BlockwaveAudioProcessorEditor::getSettleAnchorId() const
{
    if (auto* a = settleShield.getAnchor())
        return a->getComponentID();
    return {};
}

void BlockwaveAudioProcessorEditor::notePointerMoved (juce::Point<int> screenPos)
{
    settleShield.notePointer (screenPos);
}

void BlockwaveAudioProcessorEditor::setVirtualPointer (juce::Point<int> screenPos)
{
    virtualPointer = screenPos;
    notePointerMoved (screenPos);   // exactly what a real movement would do
}

void BlockwaveAudioProcessorEditor::setActiveTab (Tab tab)
{
    activeTab = tab;
    craftTab.setVisible (tab == Tab::craft);   // visibilityChanged resyncs it
    tweakTab.setVisible (tab == Tab::tweak);
    craftTabBtn.setToggleState (tab == Tab::craft, juce::dontSendNotification);
    tweakTabBtn.setToggleState (tab == Tab::tweak, juce::dontSendNotification);
}

void BlockwaveAudioProcessorEditor::setUiScale (int scalePercent, bool writeGlobal)
{
    // Snap to the slider steps: 100 / 125 / 150 / 175 / 200.
    const int snapped = blockwave::GlobalSettings::sanitiseScale (scalePercent);
    uiScale = snapped == 0 ? 100 : snapped;

    // The content stays a fixed 832x456 canvas; only the transform changes.
    // 100% / 200% are exact integer transforms (pixel-perfect path); the
    // fractional steps stay chunky because everything is drawn with solid
    // integer-coordinate fillRects (no filtered image scaling anywhere).
    content.setTransform (
        juce::AffineTransform::scale (static_cast<float> (uiScale) / 100.0f));

    // kCanvasW/H are multiples of 8, so W*pct and H*pct divide exactly by
    // 100 for every step — the window size is always a whole pixel count.
    setSize ((kCanvasW * uiScale) / 100, (kCanvasH * uiScale) / 100);
    proc.apvts.state.setProperty ("uiScale", uiScale, nullptr);   // session state
    if (writeGlobal)
        settings.setUiScalePercent (uiScale);                     // this machine
    topBar.setScalePercent (uiScale);
}

void BlockwaveAudioProcessorEditor::showPresetBrowser (bool shouldShow)
{
    if (shouldShow)
        savePanel.setVisible (false);
    browser.setVisible (shouldShow);
    if (shouldShow)
        browser.toFront (true);
}

void BlockwaveAudioProcessorEditor::showSavePanel (bool shouldShow)
{
    if (shouldShow)
    {
        browser.setVisible (false);
        savePanel.open (proc.getPresetName(), proc.getPresetCategory());
    }
    else
    {
        savePanel.setVisible (false);
    }
}
