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

    // ---- wiring (all message thread) ---------------------------------------
    topBar.onBrowse = [this] { showPresetBrowser (! browser.isVisible()); };
    topBar.onSave = [this] { showSavePanel (true); };
    topBar.onPresetChanged = [this]
    {
        browser.refresh();
        craftTab.refreshFromProcessor();          // preset carries a craft grid
    };
    topBar.onScaleChange = [this] (int s) { setUiScale (s); };

    browser.onLoad = [this] (int index)
    {
        juce::String err;
        proc.loadPresetAtIndex (index, err);
        topBar.refresh();
        browser.refresh();
        craftTab.refreshFromProcessor();
    };
    browser.onClose = [this] { showPresetBrowser (false); };

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

    // "uiScale" session property is a percent (100..200). Sessions saved
    // before the percent scheme stored 1 or 2 — anything < 10 is one of those
    // legacy integers, so read it as 100/200.
    int storedScale = static_cast<int> (
        proc.apvts.state.getProperty ("uiScale", 100));
    if (storedScale < 10)
        storedScale *= 100;
    setUiScale (storedScale);
}

BlockwaveAudioProcessorEditor::~BlockwaveAudioProcessorEditor()
{
    // The editor can be destroyed with a key still down (host closes the
    // window mid-click). Release everything the UI is holding before the key
    // strip goes away; the processor releases only ITS notes, so anything the
    // host is playing keeps sounding.
    proc.uiAllNotesOff();
    content.setLookAndFeel (nullptr);
}

void BlockwaveAudioProcessorEditor::setActiveTab (Tab tab)
{
    activeTab = tab;
    craftTab.setVisible (tab == Tab::craft);   // visibilityChanged resyncs it
    tweakTab.setVisible (tab == Tab::tweak);
    craftTabBtn.setToggleState (tab == Tab::craft, juce::dontSendNotification);
    tweakTabBtn.setToggleState (tab == Tab::tweak, juce::dontSendNotification);
}

void BlockwaveAudioProcessorEditor::setUiScale (int scalePercent)
{
    // Snap to the cycle steps: 100 / 125 / 150 / 175 / 200.
    const int clamped = juce::jlimit (100, 200, scalePercent);
    uiScale = 100 + 25 * ((clamped - 100 + 12) / 25);

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
    topBar.setScaleLabel (uiScale);
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
