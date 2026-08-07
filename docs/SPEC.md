# BLOCKWAVE — Product Spec v1.0

One-line pitch: a free synth where **every sound is a square** — from NES grit to icy PWM pads — and beginners build patches by placing blocks on a 3×3 crafting grid.

## Formats & targets

- macOS: VST3 + AU + Standalone (local dev & test). Windows x64: VST3 built unattended in GitHub Actions CI. Both platforms ship at v1.0; unsigned binaries with install instructions (code signing deferred).
- Fixed-size pixel UI with a 100 / 125 / 150 / 175 / 200 % scale **slider** in the top bar (5 discrete notches, snapping, current percent in a readout chip). **The scale is applied on mouse-UP, never mid-drag**: the slider lives inside the component it rescales, so applying a step while the button is held moves and rescales the slider under a stationary cursor, the same physical position maps to a different notch, and the window oscillates between two sizes (reported by the producer). A drag therefore only moves a *pending* step — the handle and the readout follow the cursor live, the readout turning lava and the committed notch keeping an ice pip so "here now / landing there" is readable — and exactly one apply fires on release, only if the step actually changed. Wheel steps are coalesced behind a 140 ms one-shot timer so a single trackpad flick is one apply, not five; arrow keys are one discrete step per press and apply immediately. 100% and 200% are pixel-perfect integer scales; the fractional steps stay chunky-sharp via nearest-neighbour rendering — uneven pixel sizes are acceptable, blur is not. *(Changed from the original "1x/2x integer scaling toggle" — approved by producer + architect. The architect later proposed dropping back to integer 1x/2x/3x steps only; the **producer rejected that** and kept the percent scheme, replacing the cycling button with the slider so all five stops are visible and directly reachable.)*
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
| cave_hp | 20..2000 Hz | 20 | *(addendum, Phase-6 feedback)* HP on the CAVE wet input; 20 = bit-exact off; log taper, smoothed |
| dly_hp | 20..2000 Hz | 20 | *(addendum)* HP on the DELAY line input (echoes filtered once, not per repeat); 20 = off |
| crush_hp | 20..2000 Hz | 20 | *(addendum)* HP into the CRUSH quantizer (pre-quantize, wet path only); 20 = off |
| cave_lp | 200..20000 Hz | 20000 | *(addendum 2, Phase-6 feedback)* LP on the CAVE wet input; 20000 = bit-exact off; log taper (centre 2 kHz), smoothed |
| dly_lp | 200..20000 Hz | 20000 | *(addendum 2)* LP on the DELAY line input (echoes filtered once, not per repeat); 20000 = off |
| crush_lp | 200..20000 Hz | 20000 | *(addendum 2)* LP into the CRUSH quantizer (pre-quantize, wet path only); 20000 = off |

*Addendum note: the three `*_hp` parameters were appended to the frozen table
(append-only rule — no existing ID, order, range or taper changed) after the
Phase-6 listening review. One-pole (6 dB/oct) TPT high-pass on each FX's WET
path only; the dry signal is never filtered.*

*Addendum 2 note: the three `*_lp` parameters are the symmetric partners of
the `*_hp` trio, appended after them under the same append-only rule (table
61 → 64 → 67; no existing index moved). Same one-pole (6 dB/oct) TPT topology,
same insertion point per effect, and the HP runs first so HP + LP together read
as one band-pass on that effect's input. At the 20 kHz maximum the filter is
not run at all and its state is cleared, so the default is bit-exact identical
to the pre-addendum engine (every existing golden render is the proof).
Because a one-pole low-pass parked at 20 kHz is not an identity filter, the
LP's wet contribution is faded in linearly from 0 at 20 kHz to fully wet at
16 kHz, and its state is primed to the input on engage — so switching it in or
out is click-free at both ends. The `*_lp` cells are not on the TWEAK panel
yet; they are reachable through host automation and presets meanwhile.*

Non-automatable state: craft grid contents (base + 8 cells + 8 cell weights), preset name/category, UI scale (session copy; see §UI for the global copy).

## UI

Two tabs, top bar always visible.

- **Top bar:** logo, preset ◀ ▶, preset browser button, SAVE, ★ favourite, RAW toggle (LED-style), master volume, UI scale slider. Three groups on the 8-px grid — presets, sound, view — separated by 40 px of night.
- **CRAFT tab (default):** 3×3 grid center-left; material palette (draggable blocks) on the right; big auto-generated patch name; DICE (random materials), MUTATE and CLEAR (empty the bench) buttons; a 1.5-octave clickable keyboard strip at the bottom for instant audition. This is the beginner home — full spec in `CRAFT_GRID.md`.
- **Hidden per-block MIX** *(producer request; the control for `craft.weights`. Supersedes the always-visible mix rail of the previous pass — the producer specified this interaction in its place and it is quoted below verbatim)*: the mix control is **not visible at all** on a block by default. **Hovering a filled block** reveals a 3×5 bitmap **MIX** in its **top-right corner** — bare letters in a muted tone, **no outline, no bevel, no backing plate**. **Hovering the letters highlights them** (muted mid-grey → the block's opposite extreme, so the jump reads on a light block and a dark one alike). **Clicking the label opens the mix knob on the block; clicking again hides it.** The expanded state is **pure UI state, per cell**: never written to a preset or the session, cleared when the cell is emptied, when a new block is placed on it, and whenever a preset replaces the bench; it travels with its block across a move. **Keyboard: `M` toggles the knob for the focused cell** (the label is also drawn while a cell has keyboard focus, so the shortcut advertises itself).
  - **Hit-testing** — the knob must never eat the block's drag gesture, because positions are load-bearing for recipes. The **MIX label owns a 16×12 box at the top-right and owns the CLICK only**: the press arms on mouseDown and toggles on mouseUp, so as soon as the pointer travels 4 px the same press becomes a normal block drag-and-drop (you can drag a block by its label). The **knob owns a 28×28 box at the bottom-right, and only while it is open**; a vertical drag there sets the weight. The other ~80 % of the cell is unchanged: click selects, drag moves, right-click clears. Right-click and an ARMED material are checked first and apply to every pixel of the cell, label and knob included, so the guided click-cell-then-click-material flow is never dead anywhere. The wheel nudges 5 % **only over an open knob** — with no control on screen there are no invisible edits. UP/DOWN adjust the mix while that cell's knob is open (SHIFT = 25 %) and otherwise walk the bench by rows; LEFT/RIGHT always walk the whole grid in reading order with wrap.
  - **What stays visible without touching anything**: the block art **darkens in 5 discrete steps** as the weight drops (each step blends the sprite's palette toward night *before* rasterising, so the block stays flat pixel art — no translucent wash, nothing to blur at 125/175 %), and anything below 100 % keeps a small persistent `nn%` tag in the corner, so a turned-down bench is readable at a glance. The tag is also shown at 100 % while the knob is open, and a knob drag or a keyboard/wheel nudge raises a big `nn%` badge for about half a second. Empty cells have no mix UI at all.
  - Weights never affect recipe detection or the auto-name, and nothing in the UI implies they do.
- **TWEAK tab:** the full synth — OSC / FILTER / ENV / LFO / FX sections as chunky block panels. Every knob a stepped pixel knob (pre-rendered 16-frame look or procedurally drawn), values shown in a bitmap-font readout. Cells are 48 px wide except the FX row (CRUSH / DELAY / CAVE / MASTER), which runs at 40 px: adding the three `*_lp` addendum-2 cells took that row to 18 cells and at 48 px a row holds at most 15 on the fixed canvas. Flagged, not squeezed silently — see the layout note in `plugin/ui/TweakTab.cpp`.
- **Preset browser:** list grouped by category (LEAD, BASS, PLUCK, PAD, KEYS, CHIP, PERC, FX); each preset shows its craft recipe as a mini 3×3 icon — users learn crafting by inspecting factory sounds.
- **Preset search** *(producer request, post-Phase-7: "кнопочку с поиском пресетов добавь")*: a field across the top of the browser panel, above both panes, marked with a 7×7 pixel magnifier. It filters **live on every keystroke**, **case-insensitively**, on a **substring of the preset name** (not the category, not the author), across the **whole bank** — both banks, ignoring whatever folder is selected — with the hits still grouped under the usual category headers. The field shows the match count (`19 HITS` / `NO HITS` in lava) and carries an X button to clear; `ESC` clears a live query, and `ESC` on an empty field still closes the browser exactly as before. Empty result state reads `NO SOUND BY THAT NAME`. The title-bar readout switches from `n SOUNDS` to `SEARCH: <query>` while a query is live.
  - **Search vs folder — last one touched wins, in both directions, with no third state**: typing overrides the folder (the list goes bank-wide and the folder tree greys out), and touching a folder at all — clicking one, arrowing through the tree, or `selectFolder()` from a tool — clears the query and hands the list straight back to that folder.
  - **Focus**: the browser owns the keyboard itself (there is no child text editor), so focus is a *zone* — SEARCH / FOLDERS / LIST — and **only the SEARCH zone consumes plain letters**. That is what keeps every existing binding alive: `F` still stars the highlighted row, because `F` only types while the field is the active zone. `TAB` / `SHIFT+TAB` cycle SEARCH → FOLDERS → LIST → SEARCH; `CTRL/CMD+F` jumps to the field from anywhere (not a bare letter, so it can never collide with typing); clicking the field, a folder or a row moves the zone there. In the SEARCH zone `BACKSPACE` deletes and `UP` / `DOWN` / `RETURN` drop into the result list, so you can type-then-arrow without `TAB`. The browser always opens on the FOLDERS zone with an empty query.
  - The field takes a 24 px band off the top of both panes, so the list shows 21 rows instead of 23 on the fixed canvas. Nothing else moved.
- **FAVORITES** *(added on Kirill's direct request, post-Phase-6 — producer-requested addition to spec, not scope expansion by the team)*: a ★ toggle on every browser row (click stars without loading; `F` stars the focused row) and a matching ★ in the top bar for the current preset. A FAVORITES folder sits above ALL in the browser tree with a live count, listing starred presets from both banks grouped by category; empty, it reads "STAR A PRESET TO PIN IT". Stars persist in `Documents/BLOCKWAVE/Favorites.json` (created lazily on the first star, alongside `Discoveries.json`), keyed by `CATEGORY/NAME` so they survive restarts, rescans and re-saves for both factory and user presets.

- **UI scale memory** *(producer request, architect-backed)*: the chosen scale is remembered **globally**, not just per project. It is stored in two places and read with a fixed precedence — (1) the session property `uiScale` in the plugin state, if the project carries one, so an old project reopens at exactly the size it was saved with; (2) else the machine-wide `Documents/BLOCKWAVE/Settings.json` (`plugin/GlobalSettings.h`, created lazily on the first scale change, alongside `Discoveries.json` and `Favorites.json`); (3) else 100 %. Moving the slider writes **both**, so a fresh instance in a brand new project comes up at the size the user actually works at. Restoring on open writes only the session copy — a user who never touches the slider never gets a settings file.

Look: original pixel art, 8-px grid discipline, chunky bevels, dithered gradients. Palette (original, not sampled from any game): stone gray `#8b8b8b`, dirt brown `#7a5230`, grass green `#5cab3f`, ice blue `#9fd8ff`, lava orange `#ff7b1c`, night `#1c1c24`. Bitmap font drawn from scratch. UI drawn via `juce::Graphics` with `setImageResamplingQuality(low)` / nearest-neighbour scaling for crisp pixels.

## Preset format

JSON, embedded factory bank in BinaryData + user folder `%USERPROFILE%/Documents/BLOCKWAVE/Presets/`.

```json
{
  "formatVersion": 1,
  "name": "FROSTBYTE",
  "category": "PAD",
  "author": "BLOCKWAVE Factory",
  "craft": {
    "base": "PAD",
    "cells":   ["ICE","ICE","ICE","","","","","CLOUD"],
    "weights": [1, 1, 0.5, 1, 1, 1, 1, 0.25]
  },
  "params": { "filt_cutoff": 1450.0, "cave_mix": 0.35 }
}
```

`craft` is applied first (deterministic function, see CRAFT_GRID.md), then `params` overrides on top. Factory presets keep overrides minimal so the recipe visibly explains the sound.

**`craft.weights` — per-cell WEIGHT / MIX** *(producer-requested for v1.0; the architect had deferred it to v1.1 and the producer overrode that, with the semantics below fixed by him)*. Each entry is that cell's contribution to its material's delta set, `0..1` where `1` = 100% (the default). It scales how much the material changes the sound; it is **never** part of recipe identity.

- **Optional, and its absence means 100%.** A `craft` object with no `weights` key — every one of the 128 factory presets, every user preset saved before this build, every hand-written JSON — reads as all placed cells at `1.0`, which crafts bit-identically to the pre-weight engine. Nothing needs migrating or rewriting.
- Out-of-range values clamp to `0..1`; non-numeric entries and missing trailing entries default to `1.0`; a shorter array is legal, a longer one is truncated to 8.
- An **empty cell's** weight is meaningless: it is never compared and never decides anything.
- On save, the key is written **only when at least one placed cell is off 1.0**, so an untouched craft still serialises to exactly the same JSON as before.
- **Recipe detection, discovery and auto-naming ignore weights entirely** — a grid with the right blocks at 30% (or 0%) still triggers its recipe and still gets the recipe's override patch. Full rules in `src/CraftEngine.h` (CELL WEIGHT block) and `docs/CRAFT_GRID.md`.

## Out of scope for v1 (do not build; log as proposals)

Arpeggiator/sequencer, mod matrix, MPE, mac/AU builds, skins, MIDI-learn beyond host automation, online features.
