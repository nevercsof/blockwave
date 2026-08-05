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
#include <cstring>

namespace blockwave
{

// Engine limits. See BlockwaveEngine.h for the oscillator-count cap rationale.
constexpr int kMaxVoices        = 16;
constexpr int kMaxUnison        = 8;
constexpr int kMaxUnisonStacks  = 128;  // total A+B unison stacks across all voices

enum class NoiseMode  : int { longMode = 0, shortMode = 1 };
enum class VoiceMode  : int { poly = 0, mono = 1, legato = 2 };
enum class GlideMode  : int { always = 0, legato = 1 };
enum class FilterType : int { lp24 = 0, lp12 = 1, bp = 2, hp = 3 };
enum class LfoShape   : int { square = 0, tri = 1, sampleHold = 2 };
enum class Lfo2Dest   : int { pitch = 0, cutoff = 1, pw = 2, vol = 3 };

// Plain POD parameter snapshot. Phase 2 maps APVTS parameters (SPEC IDs)
// onto these fields 1:1; until then the engine ships with SPEC defaults.
// Field names mirror the SPEC parameter IDs exactly (complete since Phase 5:
// the FX block fields live at the end of the struct).
struct ParamSnapshot
{
    // Oscillators
    bool  oscA_on     = true;
    bool  oscB_on     = false;
    bool  sub_on      = false;
    bool  noise_on    = false;
    int   oscA_oct    = 0;      // -2..+2
    int   oscB_oct    = 0;
    int   oscA_semi   = 0;      // -12..+12
    int   oscB_semi   = 0;
    float oscA_fine   = 0.0f;   // cents, -100..+100
    float oscB_fine   = 0.0f;
    float oscA_pw     = 50.0f;  // percent, 1..99
    float oscB_pw     = 50.0f;
    float oscA_level  = 0.8f;   // 0..1
    float oscB_level  = 0.8f;
    bool  oscB_sync   = false;  // hard sync B -> A master
    int   sub_oct     = -1;     // -1 or -2
    float sub_level   = 0.7f;
    NoiseMode noise_mode = NoiseMode::longMode;
    float noise_level = 0.5f;

    // Unison / voices
    int   uni_count   = 1;      // 1..8 (engine may cap; see BlockwaveEngine.h)
    float uni_detune  = 15.0f;  // cents spread
    float uni_spread  = 0.5f;   // 0..1 stereo width
    VoiceMode voice_mode = VoiceMode::poly;
    int   poly_count  = 8;      // 1..16
    float glide_time  = 0.0f;   // seconds, 0..2
    GlideMode glide_mode = GlideMode::legato;

    // Filter
    FilterType filt_type = FilterType::lp24;
    float filt_cutoff   = 20000.0f; // Hz
    float filt_res      = 0.1f;     // 0..1
    float filt_env      = 0.0f;     // -1..+1 (ENV2 -> cutoff)
    float filt_keytrack = 0.0f;     // 0..1

    // Envelopes (seconds / level)
    float env1_a = 0.003f, env1_d = 0.120f, env1_s = 0.8f, env1_r = 0.080f;
    float env2_a = 0.003f, env2_d = 0.200f, env2_s = 0.0f, env2_r = 0.100f;
    float env2_pitch = 0.0f;        // semitones, -48..+48

    // LFOs. rate fields: when *_sync is true the value is the note length in
    // whole notes (1.0 = 1/1, 0.25 = 1/4, ...); when false it is Hz.
    float lfo1_rate = 1.0f;  bool lfo1_sync = true;  float lfo1_pwm = 0.0f;
    float lfo2_rate = 0.25f; bool lfo2_sync = true;
    LfoShape lfo2_shape = LfoShape::tri;
    float lfo2_amt = 0.0f;          // -1..+1
    Lfo2Dest lfo2_dest = Lfo2Dest::cutoff;

    // Global
    float vel_amp     = 0.5f;   // velocity -> amp depth
    bool  raw         = false;  // bypass polyBLEP
    float master_gain = 0.0f;   // dB, -60..+6

    // FX block (Phase 5). IDs frozen since Phase 2; fields appended so the
    // layout of everything above is untouched.
    int   crush_bits   = 16;    // 16..1 bit depth
    int   crush_down   = 1;     // 1..64x sample-rate divide (hold)
    float crush_mix    = 0.0f;  // 0..1
    int   dly_time     = 4;     // kDlyTimeChoices index (4 = "1/4"), synced only
    float dly_fb       = 0.35f; // 0..0.9
    bool  dly_pingpong = true;
    float dly_mix      = 0.0f;  // 0..1
    float cave_size    = 0.5f;  // 0..1 -> FDN loop lengths + RT60
    float cave_damp    = 0.5f;  // 0..1 -> in-loop damping lowpass
    float cave_mix     = 0.0f;  // 0..1

    // FX wet-path high-pass cutoffs (Phase-6 producer feedback; appended per
    // the frozen-table append-only rule). 20 Hz = hard bypass (bit-exact).
    // Each filters ONLY the signal entering its effect — reverb/delay tails
    // and crushed wet stop rumbling while the dry path stays untouched.
    float cave_hp      = 20.0f; // Hz, 20..2000 log
    float dly_hp       = 20.0f;
    float crush_hp     = 20.0f;

    // FX wet-path low-pass cutoffs (Phase-6 producer feedback, addendum 2 —
    // the symmetric partner of the HP trio, same append-only rule).
    // 20000 Hz = hard bypass (bit-exact). Each filters ONLY the signal
    // entering its effect, at exactly the same point as the matching HP.
    float cave_lp      = 20000.0f; // Hz, 200..20000 log
    float dly_lp       = 20000.0f;
    float crush_lp     = 20000.0f;
};

// Single-writer (message thread) / single-reader (audio thread) snapshot
// exchange. Seqlock: the reader retries on a torn read and falls back to its
// previous good copy — no locks, no allocation on either side.
class ParamStore
{
public:
    void set (const ParamSnapshot& s) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);        // -> odd
        std::memcpy (&data, &s, sizeof (ParamSnapshot));
        seq.fetch_add (1, std::memory_order_release);        // -> even
    }

    // Returns true and fills out on success; on persistent contention leaves
    // out untouched (caller keeps its previous copy).
    bool read (ParamSnapshot& out) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto s1 = seq.load (std::memory_order_acquire);
            if ((s1 & 1u) != 0)
                continue;
            ParamSnapshot tmp;
            std::memcpy (&tmp, &data, sizeof (ParamSnapshot));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == s1)
            {
                out = tmp;
                return true;
            }
        }
        return false;
    }

private:
    ParamSnapshot data {};
    std::atomic<std::uint32_t> seq { 0 };
};

} // namespace blockwave
