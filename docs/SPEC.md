# BLOCKWAVE — Product Spec v1.0

One-line pitch: a free synth where **every sound is a square** — from NES grit to icy PWM pads — and beginners build patches by placing blocks on a 3×3 crafting grid.

## Formats & targets

- macOS: VST3 + AU + Standalone (local dev & test). Windows x64: VST3 built unattended in GitHub Actions CI. Both platforms ship at v1.0; unsigned binaries with install instructions (code signing deferred).
- Fixed-size pixel UI with a 100 / 125 / 150 / 175 / 200 % scale cycler (top-bar button shows the current value). 100% and 200% are pixel-perfect integer scales; the fractional steps stay chunky-sharp via nearest-neighbour rendering — uneven pixel sizes are acceptable, blur is not. *(Changed from the original "1x/2x integer scaling toggle" — approved by producer + architect.)*
- 16-voice polyphony, unison up to 8 per voice (engine caps total oscillator count to stay realtime; document the cap).

## Signal path

```
[OSC A] ─┐
[OSC B] ─┤ (B can hard-sync to A)
[SUB]   ─┼─> mix -> [SVF FILTER] -> [VCA (ENV1 × velocity)] -> per-voice sum
[NOISE] ─┘                                   |
                              [CRUSH] -> [DELAY] -> [CAVE] -> [MASTER softclip]
Mod: ENV2 -> filter cutoff (bipolar) and pitch (bipolar, semitones)
     LFO1 -> pulse width of A+B (the PWM engine)
     LFO2 -> assignable: pitch / cutoff / PW / volume
```

All oscillators are square/pulse. NOISE is an NES-style **LFSR** (1-bit linear feedback shift register) — technically a square pulse train, so the "only squares" concept holds while providing hats/snares/texture. Anti-aliasing: **polyBLEP** on A/B/SUB. Global **RAW** toggle bypasses polyBLEP for authentic aliased 8-bit dirt (marketing: "RAW = true retro").

## Parameter table (IDs are permanent after Phase 2)

| ID | Range | Default | Notes |
|---|---|---|---|
| oscA_on / oscB_on / sub_on / noise_on | bool | on/off/off/off | |
| oscA_oct, oscB_oct | −2..+2 | 0 | octaves |
| oscA_semi, oscB_semi | −12..+12 | 0 | |
| oscA_fine, oscB_fine | −100..+100 | 0 | cents |
| oscA_pw, oscB_pw | 1..99 % | 50 | pulse width |
| oscA_level, oscB_level | 0..1 | 0.8 | |
| oscB_sync | bool | off | hard sync B→A master |
| sub_oct | −1 / −2 | −1 | square sub |
| sub_level | 0..1 | 0.7 | |
| noise_mode | long / short | long | LFSR mode: long≈white-ish, short=metallic NES |
| noise_level | 0..1 | 0.5 | |
| uni_count | 1..8 | 1 | unison voices (A+B together) |
| uni_detune | 0..100 | 15 | cents spread |
| uni_spread | 0..1 | 0.5 | stereo width |
| voice_mode | poly / mono / legato | poly | |
| poly_count | 1..16 | 8 | |
| glide_time | 0..2 s | 0 | log taper |
| glide_mode | always / legato | legato | |
| filt_type | LP24 / LP12 / BP / HP | LP24 | SVF (cytomic/TPT topology) |
| filt_cutoff | 20..20000 Hz | 20000 | log taper, smoothed |
| filt_res | 0..1 | 0.1 | |
| filt_env | −1..+1 | 0 | ENV2 amount |
| filt_keytrack | 0..1 | 0 | |
| env1_a/d/s/r | 0..5 s (A/D/R), 0..1 (S) | 3ms / 120ms / 0.8 / 80ms | amp ADSR, log tapers |
| env2_a/d/s/r | same | 3ms / 200ms / 0 / 100ms | mod ADSR |
| env2_pitch | −48..+48 st | 0 | pitch envelope amount (plucks, drums, TNT drops) |
| lfo1_rate | synced 8/1..1/32 or 0.01..40 Hz | 1/1, sync on | |
| lfo1_sync | bool | on | |
| lfo1_pwm | 0..1 | 0 | LFO1→PW depth (both oscs) |
| lfo2_rate / lfo2_sync | as LFO1 | 1/4, on | |
| lfo2_shape | square / tri / s&h | tri | |
| lfo2_amt | −1..+1 | 0 | |
| lfo2_dest | pitch / cutoff / pw / vol | cutoff | |
| crush_bits | 16..1 | 16 | bit depth |
| crush_down | 1..64× | 1 | sample-rate divide |
| crush_mix | 0..1 | 0 | |
| dly_time | synced 1/1..1/32 (incl. dotted) | 1/4 | tempo-synced only in v1 |
| dly_fb | 0..0.9 | 0.35 | |
| dly_pingpong | bool | on | |
| dly_mix | 0..1 | 0 | |
| cave_size | 0..1 | 0.5 | dark, cavernous algorithmic reverb |
| cave_damp | 0..1 | 0.5 | |
| cave_mix | 0..1 | 0 | |
| vel_amp | 0..1 | 0.5 | velocity→amp depth |
| raw | bool | off | polyBLEP bypass |
| master_gain | −60..+6 dB | 0 | + fixed transparent softclip ceiling |

Non-automatable state: craft grid contents (base + 8 cells), preset name/category, UI scale.

## UI

Two tabs, top bar always visible.

- **Top bar:** logo, preset ◀ ▶, preset browser button, SAVE, RAW toggle (LED-style), master volume, UI scale cycler (100–200 %).
- **CRAFT tab (default):** 3×3 grid center-left; material palette (draggable blocks) on the right; big auto-generated patch name; DICE (random materials), MUTATE and CLEAR (empty the bench) buttons; a 1.5-octave clickable keyboard strip at the bottom for instant audition. This is the beginner home — full spec in `CRAFT_GRID.md`.
- **TWEAK tab:** the full synth — OSC / FILTER / ENV / LFO / FX sections as chunky block panels. Every knob a stepped pixel knob (pre-rendered 16-frame look or procedurally drawn), values shown in a bitmap-font readout.
- **Preset browser:** list grouped by category (LEAD, BASS, PLUCK, PAD, KEYS, CHIP, PERC, FX); each preset shows its craft recipe as a mini 3×3 icon — users learn crafting by inspecting factory sounds.

Look: original pixel art, 8-px grid discipline, chunky bevels, dithered gradients. Palette (original, not sampled from any game): stone gray `#8b8b8b`, dirt brown `#7a5230`, grass green `#5cab3f`, ice blue `#9fd8ff`, lava orange `#ff7b1c`, night `#1c1c24`. Bitmap font drawn from scratch. UI drawn via `juce::Graphics` with `setImageResamplingQuality(low)` / nearest-neighbour scaling for crisp pixels.

## Preset format

JSON, embedded factory bank in BinaryData + user folder `%USERPROFILE%/Documents/BLOCKWAVE/Presets/`.

```json
{
  "formatVersion": 1,
  "name": "FROSTBYTE",
  "category": "PAD",
  "author": "BLOCKWAVE Factory",
  "craft": { "base": "PAD", "cells": ["ICE","ICE","ICE","","","","","CLOUD"] },
  "params": { "filt_cutoff": 1450.0, "cave_mix": 0.35 }
}
```

`craft` is applied first (deterministic function, see CRAFT_GRID.md), then `params` overrides on top. Factory presets keep overrides minimal so the recipe visibly explains the sound.

## Out of scope for v1 (do not build; log as proposals)

Arpeggiator/sequencer, mod matrix, MPE, mac/AU builds, skins, MIDI-learn beyond host automation, online features.
