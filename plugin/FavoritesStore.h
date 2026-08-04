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

// FAVORITES persistence (producer request, Kirill — see docs/SPEC.md).
//
// A set of preset identity keys, stored as JSON at
// ~/Documents/BLOCKWAVE/Favorites.json (%USERPROFILE%\Documents\... on
// Windows) — the same folder as Discoveries.json and Presets/. The file is
// created LAZILY: only when the first favorite is added. A plain startup with
// no favorites never writes anything.
//
// Identity key: "CATEGORY/NAME", both upper-cased and trimmed (see
// PresetLibrary::favoriteKey). Filenames are NOT usable as identity: factory
// presets live in BinaryData and have no file at all, and user preset
// filenames go through juce::File::createLegalFileName, so they differ from
// the display name. Category+name is what the browser shows, what the preset
// JSON carries, and what survives a rescan, a reinstall and a folder move.
// Collisions (same name in the same category in both banks) intentionally
// share one favorite flag — the browser shows them as the same sound.
//
// Threading: message thread ONLY. Mirrors src/CraftJson.h's DiscoveryStore
// (same lazy-creation and setFile() injection pattern) so tests and
// tools/screenshots never touch the user's real file.

#include <juce_core/juce_core.h>

namespace blockwave
{

class FavoritesStore
{
public:
    explicit FavoritesStore (const juce::File& fileToUse) : file (fileToUse) { reload(); }

    static juce::File defaultFile()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("BLOCKWAVE").getChildFile ("Favorites.json");
    }

    // Test/tool injection point; reloads from the new location.
    void setFile (const juce::File& f)
    {
        file = f;
        reload();
    }

    juce::File getFile() const { return file; }

    void reload()
    {
        keys.clearQuick();
        if (! file.existsAsFile())
            return;
        const auto root = juce::JSON::parse (file.loadFileAsString());
        if (auto* list = root.getProperty ("favorites", juce::var()).getArray())
            for (const auto& v : *list)
            {
                const auto k = v.toString();
                if (k.isNotEmpty() && ! contains (k))
                    keys.add (k);
            }
    }

    bool contains (const juce::String& key) const
    {
        for (const auto& k : keys)
            if (k.equalsIgnoreCase (key))
                return true;
        return false;
    }

    // Adds/removes and persists. Returns the NEW state (true = favorited).
    // An empty key is ignored and reports false.
    bool toggle (const juce::String& key)
    {
        if (key.isEmpty())
            return false;
        const int i = indexOf (key);
        if (i >= 0)
            keys.remove (i);
        else
            keys.add (key);
        save();
        return i < 0;
    }

    bool add (const juce::String& key)
    {
        if (key.isEmpty() || contains (key))
            return false;
        keys.add (key);
        save();
        return true;
    }

    bool remove (const juce::String& key)
    {
        const int i = indexOf (key);
        if (i < 0)
            return false;
        keys.remove (i);
        save();
        return true;
    }

    // Number of KEYS on file — including keys whose preset is not currently
    // installed. PresetLibrary::getNumFavorites() is the count you show in
    // the UI (only presets that actually exist right now).
    int getNumKeys() const { return keys.size(); }
    const juce::StringArray& getKeys() const { return keys; }

private:
    int indexOf (const juce::String& key) const
    {
        for (int i = 0; i < keys.size(); ++i)
            if (keys[i].equalsIgnoreCase (key))
                return i;
        return -1;
    }

    void save()
    {
        auto dir = file.getParentDirectory();
        if (! dir.isDirectory() && dir.createDirectory().failed())
            return;                              // non-fatal: stays in memory
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("formatVersion", 1);
        juce::Array<juce::var> list;
        for (const auto& k : keys)
            list.add (k);
        obj->setProperty ("favorites", list);
        file.replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    juce::File file;
    juce::StringArray keys;
};

} // namespace blockwave
