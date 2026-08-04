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

// Preset browser DATA MODEL (Phase 2). No UI — Phase 3 builds the browser
// component on top of this interface.
//
// Threading: message thread ONLY. The audio thread never touches this class;
// loading a preset flows through APVTS atomic parameter writes.
//
// The user preset folder (~/Documents/BLOCKWAVE/Presets on macOS,
// %USERPROFILE%\Documents\BLOCKWAVE\Presets on Windows) is created LAZILY:
// only when a preset is actually saved, never at scan/construction time.

#include <vector>
#include <juce_core/juce_core.h>
#include "FavoritesStore.h"

namespace blockwave
{

// Canonical category order (SPEC §UI preset browser).
inline int categoryRank (const juce::String& category)
{
    static const char* order[] = { "LEAD", "BASS", "PLUCK", "PAD",
                                   "KEYS", "CHIP", "PERC", "FX" };
    for (int i = 0; i < 8; ++i)
        if (category.equalsIgnoreCase (order[i]))
            return i;
    return 8;                                    // unknown categories sort last
}

class PresetLibrary
{
public:
    struct Entry
    {
        juce::String name, category, author;
        bool isFactory = false;
        bool isFavorite = false;                 // mirrors FavoritesStore
        juce::var root;                          // parsed preset JSON
        juce::File file;                         // invalid for factory entries
    };

    explicit PresetLibrary (const juce::File& userFolderToUse)
        : userFolder (userFolderToUse) {}

    static juce::File defaultUserFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("BLOCKWAVE").getChildFile ("Presets");
    }

    juce::File getUserFolder() const { return userFolder; }

    // Factory bank (BinaryData JSON, fed by the owner at construction time).
    // Invalid JSON is skipped and reported via return value.
    bool addFactoryPresetJson (const juce::String& jsonText)
    {
        const auto parsed = juce::JSON::parse (jsonText);
        if (parsed.getDynamicObject() == nullptr)
            return false;
        const auto keep = currentName();     // before the vector reallocates
        factory.push_back (makeEntry (parsed, true, {}));
        rebuild (keep);
        return true;
    }

    // Re-lists *.json in the user folder. Does NOT create the folder.
    void rescanUserPresets()
    {
        const auto keep = currentName();     // before entries are replaced
        user.clear();
        if (userFolder.isDirectory())
        {
            for (const auto& f : userFolder.findChildFiles (
                     juce::File::findFiles, false, "*.json"))
            {
                const auto parsed = juce::JSON::parse (f.loadFileAsString());
                if (parsed.getDynamicObject() != nullptr)
                    user.push_back (makeEntry (parsed, false, f));
            }
        }
        rebuild (keep);
    }

    // ---- browsing ----------------------------------------------------------

    int getNumPresets() const { return static_cast<int> (sorted.size()); }

    const Entry& getPreset (int index) const { return *sorted[static_cast<size_t> (index)]; }

    int getCurrentIndex() const { return currentIndex; }

    void setCurrentIndex (int index)
    {
        currentIndex = getNumPresets() == 0 ? -1
                     : juce::jlimit (0, getNumPresets() - 1, index);
    }

    // Wrapping next/prev over the category-grouped order. -1 when empty.
    int getNextIndex() const
    {
        const int n = getNumPresets();
        return n == 0 ? -1 : (currentIndex + 1) % n;
    }

    int getPrevIndex() const
    {
        const int n = getNumPresets();
        return n == 0 ? -1 : (currentIndex - 1 + n) % n;
    }

    // Index of a preset by name (case-insensitive), or -1.
    int indexOfName (const juce::String& name) const
    {
        for (int i = 0; i < getNumPresets(); ++i)
            if (getPreset (i).name.equalsIgnoreCase (name))
                return i;
        return -1;
    }

    // ---- favorites (producer request; message thread only) -----------------
    //
    // Identity key is "CATEGORY/NAME" upper-cased — stable across restarts
    // and identical for factory and user presets (rationale in
    // FavoritesStore.h). The flags on Entry are a cache of the store and are
    // re-applied after every rebuild, so favorites survive rescanUserPresets()
    // and addFactoryPresetJson().

    static juce::String favoriteKey (const Entry& e)
    {
        return e.category.trim().toUpperCase() + "/" + e.name.trim().toUpperCase();
    }

    FavoritesStore& getFavorites() { return favorites; }
    const FavoritesStore& getFavorites() const { return favorites; }

    // Test/tool injection: point the store at another file and re-sync flags.
    void setFavoritesFile (const juce::File& f)
    {
        favorites.setFile (f);
        applyFavoriteFlags();
    }

    bool isFavorite (int index) const
    {
        return index >= 0 && index < getNumPresets() && getPreset (index).isFavorite;
    }

    // Flips the favorite state of a listed preset and persists it (the file
    // is created lazily on the first add). Returns the NEW state.
    bool toggleFavorite (int index)
    {
        if (index < 0 || index >= getNumPresets())
            return false;
        auto& e = *sorted[static_cast<size_t> (index)];
        const bool now = favorites.toggle (favoriteKey (e));
        applyFavoriteFlags();                    // name collisions stay in sync
        return now;
    }

    // Favorited presets that are actually installed right now. Keys of
    // deleted user presets stay on file but are NOT counted or listed.
    int getNumFavorites() const
    {
        int n = 0;
        for (const auto* e : sorted)
            if (e->isFavorite)
                ++n;
        return n;
    }

    // ---- saving ------------------------------------------------------------

    // Writes a user preset JSON (name/category read from the var). Creates
    // the user folder lazily here — the only place it is ever created.
    // Overwrites an existing user preset of the same name. Message thread.
    bool saveUserPreset (const juce::var& presetRoot, juce::String& error)
    {
        auto* obj = presetRoot.getDynamicObject();
        if (obj == nullptr) { error = "preset root is not an object"; return false; }
        const auto name = obj->getProperty ("name").toString();
        if (name.isEmpty()) { error = "preset has no name"; return false; }

        if (! userFolder.isDirectory())
        {
            const auto result = userFolder.createDirectory();
            if (result.failed())
            {
                error = "cannot create " + userFolder.getFullPathName()
                      + ": " + result.getErrorMessage();
                return false;
            }
        }

        const auto file = userFolder.getChildFile (
            juce::File::createLegalFileName (name) + ".json");
        if (! file.replaceWithText (juce::JSON::toString (presetRoot)))
        {
            error = "cannot write " + file.getFullPathName();
            return false;
        }

        rescanUserPresets();
        const int idx = indexOfName (name);
        if (idx >= 0)
            currentIndex = idx;
        return true;
    }

private:
    Entry makeEntry (const juce::var& parsed, bool isFactory, const juce::File& f) const
    {
        Entry e;
        e.name     = parsed.getProperty ("name",     "UNTITLED").toString();
        e.category = parsed.getProperty ("category", juce::String()).toString();
        e.author   = parsed.getProperty ("author",   juce::String()).toString();
        e.isFactory = isFactory;
        e.root = parsed;
        e.file = f;
        return e;
    }

    juce::String currentName() const
    {
        return currentIndex >= 0 && currentIndex < getNumPresets()
             ? getPreset (currentIndex).name : juce::String();
    }

    // Deterministic order: category rank, factory before user, then name.
    void rebuild (const juce::String& current)
    {
        sorted.clear();
        for (auto& e : factory) sorted.push_back (&e);
        for (auto& e : user)    sorted.push_back (&e);
        std::stable_sort (sorted.begin(), sorted.end(),
            [] (const Entry* a, const Entry* b)
            {
                const int ra = categoryRank (a->category), rb = categoryRank (b->category);
                if (ra != rb) return ra < rb;
                if (a->isFactory != b->isFactory) return a->isFactory;
                return a->name.compareIgnoreCase (b->name) < 0;
            });
        currentIndex = current.isNotEmpty() ? indexOfName (current)
                     : (sorted.empty() ? -1 : 0);
        if (currentIndex < 0 && ! sorted.empty())
            currentIndex = 0;

        applyFavoriteFlags();
    }

    void applyFavoriteFlags()
    {
        for (auto* e : sorted)
            e->isFavorite = favorites.contains (favoriteKey (*e));
    }

    juce::File userFolder;
    std::vector<Entry> factory, user;
    std::vector<Entry*> sorted;
    FavoritesStore favorites { FavoritesStore::defaultFile() };
    int currentIndex = -1;
};

} // namespace blockwave
