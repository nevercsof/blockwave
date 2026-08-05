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

// GLOBAL (machine-wide) UI settings — producer request, backed by the
// architect: the UI scale must survive into a NEW project on a FRESH plugin
// instance, which the per-session `uiScale` property in apvts.state cannot do
// (that property travels with the project, not with the user).
//
// Stored as JSON at ~/Documents/BLOCKWAVE/Settings.json (on Windows
// %USERPROFILE%\Documents\BLOCKWAVE\Settings.json) — the same folder as
// Discoveries.json, Favorites.json and Presets/. Same lazy-file pattern as
// FavoritesStore / DiscoveryStore: the file is written only when the user
// actually picks a scale. A plain startup never creates it.
//
// PRECEDENCE when an editor opens (BlockwaveAudioProcessorEditor's ctor):
//   1. the session value (apvts.state property "uiScale") if the project
//      carries one — so an old project keeps exactly the look it was saved
//      with, even if the global default has moved on since;
//   2. else this global value;
//   3. else 100 %.
// Picking a scale in the top bar writes BOTH (session + global).
//
// Threading: message thread ONLY (UI code). Nothing here is touched by the
// audio thread, and no instance shares mutable state with another — each
// editor owns its own GlobalSettings and re-reads the file when it opens.
//
// TEST/TOOL INJECTION: the editor builds its store from defaultFile(), which
// the process-wide setDefaultFile() hook can redirect BEFORE any editor
// exists (tools/screenshots and any future UI test do exactly that), so a
// headless run neither reads nor writes the user's real settings. The
// override is a message-thread-only test seam, never touched at runtime by
// the plugin itself.

#include <juce_core/juce_core.h>

namespace blockwave
{

class GlobalSettings
{
public:
    // The scale steps the UI offers (SPEC §UI). 0 is used throughout to mean
    // "not set" — it is never a legal stored value.
    static constexpr int kMinScalePercent = 100;
    static constexpr int kMaxScalePercent = 200;
    static constexpr int kScaleStepPercent = 25;

    explicit GlobalSettings (const juce::File& fileToUse) : file (fileToUse) { reload(); }
    GlobalSettings() : GlobalSettings (defaultFile()) {}

    // Where a plugin instance looks unless a test redirected it.
    static juce::File defaultFile()
    {
        const auto& over = overrideFile();
        if (over.getFullPathName().isNotEmpty())
            return over;
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("BLOCKWAVE").getChildFile ("Settings.json");
    }

    // Process-wide redirect for tests/tools. Pass an empty File to restore
    // the real location. Message thread only, before any editor is created.
    static void setDefaultFile (const juce::File& f) { overrideFile() = f; }

    // Per-instance redirect; reloads from the new location.
    void setFile (const juce::File& f)
    {
        file = f;
        reload();
    }

    juce::File getFile() const { return file; }

    void reload()
    {
        uiScalePercent = 0;
        if (! file.existsAsFile())
            return;
        const auto root = juce::JSON::parse (file.loadFileAsString());
        const auto v = root.getProperty ("uiScale", juce::var());
        if (v.isVoid())
            return;
        uiScalePercent = sanitiseScale (static_cast<int> (v));
    }

    // 0 when no global scale has ever been chosen (or the file is corrupt).
    int getUiScalePercent() const noexcept { return uiScalePercent; }

    // Stores the chosen scale, snapped to the 100/125/150/175/200 steps.
    // Writes the file only when the value actually changes, so the store
    // stays lazy: nothing is created until the user picks a scale.
    void setUiScalePercent (int percent)
    {
        const int snapped = sanitiseScale (percent);
        if (snapped == 0 || snapped == uiScalePercent)
            return;
        uiScalePercent = snapped;
        save();
    }

    // Snaps to the legal steps; returns 0 for anything unusable.
    static int sanitiseScale (int percent) noexcept
    {
        if (percent <= 0)
            return 0;
        if (percent < 10)
            percent *= 100;                  // legacy 1x / 2x integers
        const int clamped = juce::jlimit (kMinScalePercent, kMaxScalePercent, percent);
        return kMinScalePercent
             + kScaleStepPercent * ((clamped - kMinScalePercent + kScaleStepPercent / 2)
                                    / kScaleStepPercent);
    }

private:
    static juce::File& overrideFile()
    {
        static juce::File f;                 // message thread only (test seam)
        return f;
    }

    void save()
    {
        auto dir = file.getParentDirectory();
        if (! dir.isDirectory() && dir.createDirectory().failed())
            return;                          // non-fatal: stays in memory
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("formatVersion", 1);
        obj->setProperty ("uiScale", uiScalePercent);
        file.replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    juce::File file;
    int uiScalePercent = 0;                  // 0 = never chosen
};

} // namespace blockwave
