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

#include <atomic>
#include <cstdint>

namespace blockwave
{

// Lock-free single-producer / single-consumer ring of UI note events.
//
//   producer = message thread (the CRAFT tab's keyboard strip / editor teardown)
//   consumer = audio thread   (BlockwaveAudioProcessor::processBlock)
//
// Deliberately NOT juce::MidiKeyboardState or juce::MidiMessageCollector: both
// take a lock in the call the audio thread makes, which CLAUDE.md forbids.
// Deliberately NOT juce::AbstractFifo either — a fixed power-of-two ring with
// two release/acquire indices is ~30 lines, has no prepare/finish protocol to
// get wrong, and keeps this header pure C++ so both test suites can use it.
//
// The ring lives in the processor, never in the editor, so an editor that
// closes mid-flight cannot invalidate anything the audio thread is reading.
//
// OVERFLOW POLICY (see also PluginProcessor::drainUiMidi): a push into a full
// ring drops the NEW event (dropping the oldest is not safe from the producer
// side) and raises a sticky overflow flag. The consumer turns that flag into a
// release of every UI-held note, so a dropped note-OFF can never leave a note
// stuck on. Because the flag is handled in the same drain that applied the
// surviving events, an overflowing burst is released before a single sample is
// rendered — the audible result is silence, never a stuck note. Capacity is
// 128 events; a mouse-driven keyboard cannot produce anywhere near that
// between two audio blocks, so this path is a safety net, not a working mode.
struct UiMidiEvent
{
    enum Type : std::uint8_t { none = 0, noteOn = 1, noteOff = 2, allNotesOff = 3 };

    std::uint8_t type     = none;
    std::uint8_t note     = 0;      // 0..127
    std::uint8_t velocity = 0;      // 0..127
};

class UiMidiQueue
{
public:
    static constexpr std::uint32_t kCapacity = 128;      // power of two
    static constexpr std::uint32_t kMask     = kCapacity - 1;

    // ---- producer: message thread only ------------------------------------
    // Returns false when the ring is full (event dropped, overflow flagged).
    bool push (const UiMidiEvent& e) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);
        if (w - r >= kCapacity)                          // unsigned wrap-safe
        {
            overflow.store (true, std::memory_order_release);
            return false;
        }
        slots[w & kMask] = e;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    // ---- consumer: audio thread only --------------------------------------
    bool pop (UiMidiEvent& e) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);
        if (r == w)
            return false;
        e = slots[r & kMask];
        readIndex.store (r + 1, std::memory_order_release);
        return true;
    }

    // True once per overflow burst; clears the flag.
    bool consumeOverflow() noexcept
    {
        if (! overflow.load (std::memory_order_acquire))
            return false;
        overflow.store (false, std::memory_order_release);
        return true;
    }

private:
    // Plain POD slots: the release/acquire pair on the indices publishes them.
    UiMidiEvent slots[kCapacity] {};
    std::atomic<std::uint32_t> writeIndex { 0 };
    std::atomic<std::uint32_t> readIndex  { 0 };
    std::atomic<bool> overflow { false };
};

} // namespace blockwave
