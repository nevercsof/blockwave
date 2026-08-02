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

// BLOCKWAVE headless renderer.
//
//   render <preset.json> <input.mid | notespec> <out.wav> [--sr N] [--bpm N]
//
// Preset JSON per SPEC §Preset format; "params" is applied through the same
// frozen table (src/ParamSpec.h) as the plugin's APVTS, so render and plugin
// agree exactly (craft applies in Phase 4). Missing params = SPEC defaults.
// Pass "-" as the preset to render the default patch. MIDI pitch-wheel events
// in .mid inputs drive the engine's ±2 st smoothed pitch bend.
//
// Note spec (test convenience, documented for qa-runner):
//   note:<name-or-number>:<seconds>s      e.g. note:C4:2s   note:60:1.5s
// C4 = MIDI 60. Velocity is fixed at 100/127. Sharps use '#'; e.g. note:F#3:1s.
// A 2-second release tail is rendered after the last note-off.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "BlockwaveEngine.h"
#include "PresetMapping.h"

#include <iostream>

namespace
{

// type: 0 = note-off, 1 = note-on (value = velocity),
//       2 = pitch bend (value = semitones, fixed ±2 st range).
struct NoteEvent { double timeSec; int note; float value; int type; };

int parseNoteName (const juce::String& s)
{
    if (s.containsOnly ("0123456789"))
        return s.getIntValue();

    static const char* names[] = { "C", "D", "E", "F", "G", "A", "B" };
    static const int   semis[] = {  0,   2,   4,   5,   7,   9,   11 };
    const auto up = s.toUpperCase();
    for (int i = 0; i < 7; ++i)
    {
        if (up.startsWithChar (names[i][0]))
        {
            int semi = semis[i];
            int idx  = 1;
            if (up.length() > idx && up[idx] == '#') { ++semi; ++idx; }
            const int oct = up.substring (idx).getIntValue();
            return (oct + 1) * 12 + semi;              // C4 = 60
        }
    }
    return -1;
}

bool buildEvents (const juce::String& midiArg, std::vector<NoteEvent>& events, double& lengthSec)
{
    if (midiArg.startsWith ("note:"))
    {
        auto parts = juce::StringArray::fromTokens (midiArg, ":", "");
        if (parts.size() != 3 || ! parts[2].endsWith ("s"))
            return false;
        const int note = parseNoteName (parts[1]);
        const double dur = parts[2].dropLastCharacters (1).getDoubleValue();
        if (note < 0 || note > 127 || dur <= 0.0)
            return false;
        events.push_back ({ 0.0, note, 100.0f / 127.0f, 1 });
        events.push_back ({ dur, note, 0.0f, 0 });
        lengthSec = dur + 2.0;
        return true;
    }

    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (midiArg));
    if (! f.existsAsFile())
        return false;
    juce::FileInputStream in (f);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
        return false;
    mf.convertTimestampTicksToSeconds();

    double last = 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* track = mf.getTrack (t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& m = track->getEventPointer (i)->message;
            if (m.isNoteOn())
                events.push_back ({ m.getTimeStamp(), m.getNoteNumber(), m.getFloatVelocity(), 1 });
            else if (m.isNoteOff())
                events.push_back ({ m.getTimeStamp(), m.getNoteNumber(), 0.0f, 0 });
            else if (m.isPitchWheel())   // fixed ±2 st range, matches the plugin
                events.push_back ({ m.getTimeStamp(), 0,
                                    2.0f * static_cast<float> (m.getPitchWheelValue() - 8192)
                                         / 8192.0f, 2 });
            last = std::max (last, m.getTimeStamp());
        }
    }
    std::sort (events.begin(), events.end(),
               [] (const NoteEvent& a, const NoteEvent& b) { return a.timeSec < b.timeSec; });
    lengthSec = last + 2.0;
    return ! events.empty();
}

} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (argv[i]));

    double sr = 44100.0, bpm = 120.0;
    for (int i = 0; i < args.size() - 1; ++i)
    {
        if (args[i] == "--sr")  { sr  = args[i + 1].getDoubleValue(); args.removeRange (i, 2); --i; }
        else if (args[i] == "--bpm") { bpm = args[i + 1].getDoubleValue(); args.removeRange (i, 2); --i; }
    }

    if (args.size() != 3)
    {
        std::cerr << "usage: render <preset.json|-> <input.mid|note:C4:2s> <out.wav> [--sr N] [--bpm N]\n";
        return 1;
    }

    blockwave::ParamSnapshot params;   // SPEC defaults
    if (args[0] != "-")
    {
        juce::File pf (juce::File::getCurrentWorkingDirectory().getChildFile (args[0]));
        auto parsed = juce::JSON::parse (pf.loadFileAsString());
        if (parsed.isVoid())
        {
            std::cerr << "error: cannot parse preset " << args[0] << "\n";
            return 1;
        }
        juce::String err;
        if (! blockwave::applyPresetParams (parsed, params, err))
        {
            std::cerr << "error: " << err << "\n";
            return 1;
        }
    }

    std::vector<NoteEvent> events;
    double lengthSec = 0.0;
    if (! buildEvents (args[1], events, lengthSec))
    {
        std::cerr << "error: cannot read MIDI/notespec " << args[1] << "\n";
        return 1;
    }

    blockwave::BlockwaveEngine engine;
    engine.setParams (params);
    engine.setTempo (bpm);
    engine.prepare (sr, 512);

    const int totalSamples = static_cast<int> (lengthSec * sr);
    juce::AudioBuffer<float> out (2, totalSamples);
    std::vector<float> chunkL (512), chunkR (512);

    size_t evIdx = 0;
    int pos = 0;
    while (pos < totalSamples)
    {
        int segEnd = std::min (totalSamples, pos + 512);
        while (evIdx < events.size()
               && static_cast<int> (events[evIdx].timeSec * sr) <= pos)
        {
            const auto& e = events[evIdx];
            if      (e.type == 1) engine.noteOn (e.note, e.value);
            else if (e.type == 0) engine.noteOff (e.note);
            else                  engine.setPitchBend (e.value);
            ++evIdx;
        }
        if (evIdx < events.size())
            segEnd = std::min (segEnd, std::max (pos + 1,
                        static_cast<int> (events[evIdx].timeSec * sr)));

        const int n = segEnd - pos;
        engine.process (chunkL.data(), chunkR.data(), n);
        out.copyFrom (0, pos, chunkL.data(), n);
        out.copyFrom (1, pos, chunkR.data(), n);
        pos = segEnd;
    }

    juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (args[2]));
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = outFile.createOutputStream();
    if (stream == nullptr)
    {
        std::cerr << "error: cannot open output " << args[2] << "\n";
        return 1;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sr, 2, 32, {}, 0));
    if (writer == nullptr)
    {
        std::cerr << "error: cannot create WAV writer\n";
        return 1;
    }
    stream.release();   // writer owns it now
    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    writer.reset();

    std::cout << "rendered " << totalSamples << " samples @ " << sr << " Hz -> "
              << outFile.getFullPathName() << "\n";
    return 0;
}
