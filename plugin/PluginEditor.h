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
// smoothing filter).
//
// SCALE PERSISTENCE (producer + architect). The chosen scale is stored in two
// places and read with a fixed precedence:
//   1. the SESSION property "uiScale" in apvts.state (percent; values < 10
//      are legacy 1x/2x integers, migrated on read) — travels with the
//      project, so an old project reopens looking exactly as it was saved;
//   2. else the GLOBAL store (plugin/GlobalSettings.h,
//      ~/Documents/BLOCKWAVE/Settings.json) — so a fresh instance in a brand
//      new project comes up at the size this user actually works at;
//   3. else 100 %.
// Moving the top-bar scale slider writes BOTH. Restoring on open writes only
// the session copy, which keeps the global file lazily created: a user who
// never touches the slider never gets a Settings.json.
//
// SCALE SETTLE SHIELD (adversarial review, targets 1-3). setUiScale applies
// content.setTransform() synchronously, so the instant a scale commits the
// ENTIRE canvas re-maps under a cursor that has not moved. The scale slider is
// the worst-placed control for that: 200 px wide, hard against the right edge
// of an 832 px canvas, so it has the largest displacement on the bar. The next
// input event therefore lands wherever the new mapping put that screen point,
// and two of the candidates — the master gain knob and the RAW toggle — are
// AUDIO PARAMETERS. Concretely, at 100 % -> 125 %:
//   - screen (715, 20) is the middle of the scale track before the commit and
//     inside masterKnob after it: the second notch of a slow wheel spin
//     changes the output level;
//   - screen (660, 25) is the track before and rawBtn after: the second click
//     of a double-click toggles the aliasing mode. (masterKnob has
//     setDoubleClickReturnValue, so users are trained to double-click here.)
// Patching those two coordinates would leave the class open — every step has
// its own bands, and the set of controls that can slide under the cursor is
// the whole bar. So the guard is placed one level up, in the editor.
//
// ---- KEYED ON CONTROL IDENTITY, NOT ON A CLOCK AND NOT ON A DISTANCE -------
// Two earlier versions of this guard used a proxy and each one was beaten by
// the other end of it:
//   - a fixed 350 ms window: beaten by a pause. macOS's double-click interval
//     is 500 ms by default and ~1 s with the Accessibility slider, and a slow
//     wheel notch can be a second apart, so the window expired with the cursor
//     still stale. Raising the number only moves the boundary and makes the UI
//     sticky.
//   - a fixed 8 px "re-aim" radius: beaten by a drift. The controls that slide
//     under the pointer are 16-96 px wide, so a 9 px twitch dropped the guard
//     while the hazard was still directly under the cursor. Raising the radius
//     runs into the neighbouring control.
// Both were proxying for one sentence, so the guard now IS that sentence:
//
//     an input event is stale iff the control it would actuate is not the
//     control the user was aiming at when they last looked.
//
// Which makes the mechanism a comparison of component identities, evaluated
// against the LIVE pointer at the moment the transform is applied:
//
//   aim    = the control under the pointer before the transform (inherited
//            from the previous commit if the pointer has not moved since —
//            that is what a chained wheel spin is);
//   after  = the control under the pointer once the transform has landed;
//   if (after == aim)          nothing moved under the pointer: NO SHIELD;
//   else                       arm a shield ANCHORED TO `after`, solid exactly
//                              where componentAt(pointer) == after, and down
//                              the instant that stops being true.
//
// Every failure of the two proxies closes by construction, not by a threshold:
//   - a 9 px drift inside the master knob leaves componentAt == the knob, so
//     the shield holds; a movement that leaves the knob is a re-aim by
//     definition and drops it, whether that is 3 px or 300;
//   - no clock is consulted anywhere, so a 5 s or a 5 min pause changes
//     nothing. THERE IS NO BACKSTOP TIMER, and none is needed: the shield is
//     one control large and solid only while the pointer is on that control,
//     so it cannot stick over anything the user is trying to reach;
//   - the shield can never eat an event aimed at a different control, because
//     "aimed at a different control" is literally its disarm condition;
//   - `aim`/`after` come from the live pointer, never from a position captured
//     at the event that requested the step. That closes the deferred commit
//     (the wheel coalescer can land the apply seconds and hundreds of pixels
//     later) and the keyboard commit (which has no event position at all) with
//     the same line, and removes the keyboard special case entirely: the rule
//     is device-agnostic, which it always should have been.
// Bounds, all structural:
//   - the shield NEVER anchors to the scale control itself. If the scale
//     control is what slid under the pointer, the user can simply keep
//     stepping — blocking it is the sticky failure and it is not a hazard,
//     being neither an audio parameter nor preset navigation.
//   - the keyboard is never shielded: no focus is taken and no key is
//     swallowed, so every control stays keyboard-reachable throughout.
//   - anchoring to a CONTAINER (a bare patch of the top bar, say) degenerates
//     safely: the shield is solid only where componentAt is that container,
//     i.e. only on the gaps between its children, which actuate nothing.
//   - a stray wheel notch is ROUTED BACK to the scale control when `aim` is
//     the scale control, so a slow spin keeps stepping the scale at any
//     cadence instead of grabbing whatever slid under the pointer.
// Rejected alternative: an unscaled overlay for the scale control so it never
// moves. Architecturally the cleanest, but it breaks the pixel-art bar (an
// unscaled 200 px widget floating over a 200 % canvas is a different-sized
// pixel grid sitting on the grid it is meant to be part of), so no.
//
// Tabs: CRAFT (default per SPEC — the crafting bench, Phase 4) and TWEAK
// (all 61 parameters). Top bar is always visible. Preset browser and save
// dialog are overlays. Everything runs on the message thread; parameter
// traffic goes exclusively through APVTS attachments.

#include "PluginProcessor.h"
#include "GlobalSettings.h"
#include "ui/PixelLookAndFeel.h"
#include "ui/TopBar.h"
#include "ui/TweakTab.h"
#include "ui/PresetBrowser.h"
#include "ui/CraftTab.h"

#include <optional>

class BlockwaveAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    enum class Tab { craft, tweak };

    explicit BlockwaveAudioProcessorEditor (BlockwaveAudioProcessor&);
    ~BlockwaveAudioProcessorEditor() override;

    // Also used by tools/screenshots to render every screen offscreen.
    void setActiveTab (Tab);
    Tab getActiveTab() const { return activeTab; }
    // Snapped to 100..200 by 25. writeGlobal = false is the restore path
    // (session only); the top-bar slider passes true and updates both stores.
    void setUiScale (int scalePercent, bool writeGlobal = true);
    int getUiScale() const { return uiScale; }        // percent
    void showPresetBrowser (bool shouldShow);
    void showSavePanel (bool shouldShow);

    // ---- settle shield (see the note at the top of this file) --------------
    // NO CONSTANTS. There is no window length and no slop radius, because
    // there is no clock and no distance in the mechanism: it compares which
    // control is under the pointer, and that is self-limiting.
    bool isSettlingAfterScaleChange() const { return settleShield.isArmed(); }
    // The componentID of the control the shield is anchored to — i.e. the
    // control that slid under the pointer — or empty when nothing is armed.
    juce::String getSettleAnchorId() const;
    // TRUE iff the shield's juce::Desktop global mouse listener is registered.
    // That registration IS the production disarm path, and it is also the ONLY
    // thing this returns: the answer is the presence of the registration
    // object itself, not a bool kept alongside it, so it cannot be true while
    // the listener is missing.
    bool isWatchingPointerForSettle() const { return settleShield.isWatching(); }
    // Ends the settle now. The destructor calls it, so the shield's global
    // mouse listener is gone before any member it can reach starts dying.
    void endScaleSettle();
    // "The pointer is now at this SCREEN point." Production drives this from
    // the shield's own juce::Desktop global mouse listener, which is the whole
    // disarm path. Public because a synthetic juce::MouseEvent handed straight
    // to a handler (what an automation harness does) never passes through
    // Desktop's dispatch, so it has to be able to say so.
    void notePointerMoved (juce::Point<int> screenPos);
    // HEADLESS TESTS / AUTOMATION ONLY. The guard reads the live cursor
    // through juce::Desktop's mouse source, and a headless run has no cursor
    // at all — no synthetic juce::MouseEvent ever updates that source. This
    // parks a virtual one: every later pointer read returns it, and setting it
    // runs exactly the disarm check a real movement would. Production never
    // calls this, so the real path always reads the real cursor.
    void setVirtualPointer (juce::Point<int> screenPos);
    // The deepest component the next event at this SCREEN point would be
    // delivered to, with the shield itself excluded — i.e. what the guard sees
    // when it decides. Public so the tests can state a case in the same terms
    // the guard reasons in ("is the hazard still under the cursor?") instead
    // of re-deriving the layout.
    juce::Component* controlUnderPointer (juce::Point<int> screenPos);

    blockwave::ui::TopBar& getTopBar() { return topBar; }
    blockwave::ui::CraftTab& getCraftTab() { return craftTab; }
    blockwave::ui::PresetBrowser& getBrowser() { return browser; }
    // The save panel is an overlay with no other observer. "A scale gesture
    // must never open SAVE" is one of the settle shield's acceptance clauses,
    // so the tests need to be able to look.
    bool isSavePanelVisible() const { return savePanel.isVisible(); }

    void resized() override {}                        // fixed canvas

private:
    class Content final : public juce::Component
    {
    public:
        Content() { setOpaque (true); }
        void paint (juce::Graphics&) override;
    };

    // Transparent, paints nothing, spans the whole canvas while armed but is
    // SOLID ONLY where the deepest component under the point is `anchor` — the
    // control that slid under the pointer. It has no paint() override on
    // purpose: the shield must be invisible in every screenshot and must not
    // disturb a single pixel of the pixel art.
    //
    // Mouse events do not bubble in JUCE, so NOT overriding mouseDown/mouseUp
    // /mouseDoubleClick is exactly what swallows them. mouseWheelMove is the
    // one exception — the base class DOES forward it to the parent — so it is
    // overridden both to stop that and to hand the notch to its owner. Events
    // that are NOT stale never get here at all: hitTest already let them
    // through to whatever is under the pointer.
    class SettleShield final : public juce::Component
    {
    public:
        explicit SettleShield (BlockwaveAudioProcessorEditor& ownerEditor);
        ~SettleShield() override;

        // aimedAt:      the control the user was aiming at when they last
        //               looked (what was under the pointer before the canvas
        //               moved, or the inherited aim of an unbroken chain).
        // slidUnder:    the control that is under the pointer NOW. The shield
        //               is exactly this control large and lives exactly as
        //               long as the pointer stays on it.
        // aimOwnsWheel: `aimedAt` is the scale control, so a notch arriving at
        //               a stale point still belongs to it and is handed back
        //               rather than dropped.
        void arm (juce::Component* aimedAt, juce::Component* slidUnder,
                  bool aimOwnsWheel);
        void disarm();

        bool isArmed() const noexcept { return isVisible(); }
        bool isWatching() const noexcept { return watch != nullptr; }
        juce::Component* getAnchor() const noexcept { return anchor.getComponent(); }
        juce::Component* getAim() const noexcept { return aim.getComponent(); }

        // "The pointer is now here." Disarms unless the pointer is still on
        // the anchor. No threshold: leaving the control IS the re-aim.
        void notePointer (juce::Point<int> screenPos);

        // up, isSmooth, isInertial — the routed notch has to carry the kind of
        // gesture it came from, or a momentum tail would step the scale again.
        std::function<void (bool, bool, bool)> onStaleWheel;

        // Scoped "pretend I am not here", used by the editor's own
        // component-under-pointer query so the shield never hides the answer
        // it is being asked about — and so hitTest cannot recurse into itself.
        struct ScopedTransparent
        {
            explicit ScopedTransparent (SettleShield& s) : shield (s)
            { shield.transparentToQueries = true; }
            ~ScopedTransparent() { shield.transparentToQueries = false; }
            SettleShield& shield;
            JUCE_DECLARE_NON_COPYABLE (ScopedTransparent)
        };

        bool hitTest (int x, int y) override;
        void mouseWheelMove (const juce::MouseEvent&,
                             const juce::MouseWheelDetails&) override;

    private:
        // Desktop global mouse listeners are called for EVERY mouse event in
        // the process, including the shield's own. Folding them into the
        // shield's own overrides would double-handle the notches it routes, so
        // the movement watch is a separate listener object.
        struct PointerWatch final : public juce::MouseListener
        {
            explicit PointerWatch (SettleShield& o) : owner (o) {}
            void mouseMove  (const juce::MouseEvent& e) override { note (e); }
            void mouseEnter (const juce::MouseEvent& e) override { note (e); }
            void mouseExit  (const juce::MouseEvent& e) override { note (e); }
            void mouseDown  (const juce::MouseEvent& e) override { note (e); }
            void mouseDrag  (const juce::MouseEvent& e) override { note (e); }
            void mouseUp    (const juce::MouseEvent& e) override { note (e); }

            void note (const juce::MouseEvent& e)
            {
                owner.notePointer (e.getScreenPosition());
            }

            SettleShield& owner;
        };

        // The registration IS the observable (isWatching()). There is no
        // separate flag that could stay true after someone deletes the
        // addGlobalMouseListener call — deleting it means deleting this
        // object, and the observable goes false with it.
        struct GlobalWatch
        {
            explicit GlobalWatch (juce::MouseListener& l) : listener (l)
            { juce::Desktop::getInstance().addGlobalMouseListener (&listener); }
            ~GlobalWatch()
            { juce::Desktop::getInstance().removeGlobalMouseListener (&listener); }
            juce::MouseListener& listener;
            JUCE_DECLARE_NON_COPYABLE (GlobalWatch)
        };

        BlockwaveAudioProcessorEditor& owner;
        juce::Component::SafePointer<juce::Component> anchor, aim;
        bool wheelBelongsToScale = false;
        bool transparentToQueries = false;
        PointerWatch pointerWatch { *this };
        std::unique_ptr<GlobalWatch> watch;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettleShield)
    };

    // THE WHOLE GUARD, in one place: the only path that arms a shield. Called
    // from topBar.onScaleChange — the restore-on-open call and the screenshot
    // tool go straight to setUiScale, because no cursor gesture is in flight
    // there and nothing must freeze.
    void commitScaleFromGesture (int scalePercent);
    // Where the pointer is RIGHT NOW, in screen coordinates. Read fresh at
    // every use; never a position captured at an earlier event.
    juce::Point<int> pointerScreenPos() const;
    // The deepest component the next event at this point would be delivered
    // to, with the shield itself excluded. Same walk juce::Component uses to
    // route a real click or notch, so the answer is the routing, not a model
    // of it.
    juce::Component* controlAtCanvasPoint (juce::Point<float> canvasPt);
    bool isScaleControl (juce::Component*);

    BlockwaveAudioProcessor& proc;
    // Machine-wide UI preferences (currently just the scale). Built from
    // GlobalSettings::defaultFile(), which tools/tests can redirect before
    // any editor is constructed.
    blockwave::GlobalSettings settings;
    blockwave::ui::PixelLookAndFeel lnf;
    Content content;
    blockwave::ui::TopBar topBar;
    juce::TextButton craftTabBtn { "CRAFT" }, tweakTabBtn { "TWEAK" };
    blockwave::ui::CraftTab craftTab;
    blockwave::ui::TweakTab tweakTab;
    blockwave::ui::PresetBrowser browser;
    blockwave::ui::SavePanel savePanel;
    juce::TooltipWindow tooltips;
    // LAST member on purpose: reverse-order destruction drops its global mouse
    // listener and its callback before topBar (which that callback reaches)
    // dies.
    SettleShield settleShield { *this };

    Tab activeTab = Tab::craft;
    int uiScale = 100;                                // percent
    // Headless/automation virtual cursor; empty in production, where the
    // pointer is read from juce::Desktop's own mouse source.
    std::optional<juce::Point<int>> virtualPointer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockwaveAudioProcessorEditor)
};
