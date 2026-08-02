# RECIPE NOTES — the 16 hidden recipes (Phase 4)

Design record for `data/recipes.json`. Schema and matching rules:
`docs/RECIPES_FORMAT.md`. Techniques cited by number from
`docs/SOUND_DESIGN.md` §Technique catalogue.

Cell indexing (frozen, `docs/CRAFT_GRID.md`):

```
0 1 2
3 . 4       . = BASE (centre)
5 6 7
```

In the sketches below the centre cell shows the base in brackets and `.` is an
empty cell.

**Recipes 1–8** are the spec recipes: their patterns are FROZEN against
`src/CraftEngine.h::specRecipePatterns()` and asserted by the `craft_recipe_book`
state test. Only their override patches were authored here.
**Recipes 9–16 are new** (designed in Phase 4).

## How the overrides were tuned

Every override is a *diff* on top of the plain craft result for that exact
grid. To prove each one beats its plain-craft neighbourhood, the plain result
was dumped straight out of `craftApply()` into a params-only preset (a
scratchpad tool; the path nulls against the craft path at −112 dB) and rendered
next to the recipe grid through
`build/release/render_artefacts/Release/render`. Each pair was rendered at a
low / mid / high note **two to three octaves apart** plus a category phrase
(chord, scale, groove, drum pattern or 4-second hold) at 120 BPM, and compared
on: peak/RMS/crest, spectral centroid, five-band energy split, L/R correlation,
attack time, −30 dB decay time, frame-to-frame centroid motion, DC offset, and
(for the movement recipes) a segmented fundamental-frequency trace.

Three findings shaped almost every patch:

1. **Stacked materials hit the SPEC rails.** ICE×3 rails unison to 8 and clamps
   release to the 5 s maximum; OBSIDIAN×2 rounds oscA to octave −2; TNT×2 clamps
   `env2_pitch` to −48 st; SAND×3 clamps noise to 1.0; CLOUD×8 drives `cave_mix`
   to 0.95 and the osc level to 0.50. Plain craft at 3+ copies is frequently a
   clipped extreme, which is exactly the space a hand-tuned recipe should
   reclaim.
2. **`filt_keytrack` is the playability lever.** Craft never sets it, so a
   crafted patch has one fixed cutoff for the whole keyboard. FOREST LULLABY's
   plain grid is 11 dB quieter three octaves up than in the middle. Eleven of
   the sixteen recipes spend one parameter on keytrack; that single value is what
   makes them play across two-plus octaves.
3. **`env2_pitch` sign is inverted relative to the bible** (see Open questions
   #1). Positive amounts give the descending drop the drum techniques describe.

---

## 1. PERMAFROST — PAD

```
ICE      ICE      ICE
.        [PAD]    .
.        .        .
```

- **Techniques:** 1 (PWM pad), 13 (cave ambience).
- **Intent:** the glacial, dark, cavernous money pad — deep slow PWM inside a big damped ice cave.
- **Why the override wins:** ICE×3 rails unison to 8 voices at 35 cents and clamps the release to 5 s — wash, not width. Backing unison to 7 at 22 cents, deepening the PWM to 0.62 at an 8-second cycle and darkening to 3.4 kHz measures as **+41 % more frame-to-frame spectral motion** (0.96 vs 0.68), **4× less 2–8 kHz fizz** (e_hi 0.004 vs 0.012) and a **wider chord** (L/R correlation 0.40 vs 0.63), with keytrack keeping the top octave alive.

## 2. MAGMA FLOOR — BASS

```
.        .        .
OBSIDIAN [BASS]   OBSIDIAN
.        LAVA     .
```

- **Techniques:** 6 (sub-anchored bass), LAVA grind (11-adjacent crush).
- **Intent:** a molten floor — heavy, crushed, mono, playable.
- **Why the override wins:** OBSIDIAN×2 rounds oscA to octave −2 over a 212 Hz cutoff, so plain craft is a subsonic rumble with no note in it — **e_low (120–500 Hz) is 0.009–0.040 across the range**. Putting oscA back at pitch, opening to 460 Hz with keytrack 0.5 and a 6-bit grind gives **e_low 0.014 / 0.139 / 0.556** low→high, i.e. an actual growl that tracks the keyboard, plus 1.5 dB of restored headroom (peak −1.5 dBFS vs 0.0).

## 3. QUARRY KICK — PERC

```
.        TNT      .
SAND     [PERC]   .
.        TNT      .
```

- **Techniques:** 8 (chip drums / kick).
- **Intent:** a blasting-charge kick with quarry grit.
- **Why the override wins:** TNT×2 clamps `env2_pitch` to −48 st, which in this engine sweeps the pitch **up** into the note, and SAND stacks the noise to 1.0 — a rising hiss, not a kick. Flipping to a descending +30 st / 55 ms drop with a short-LFSR crack turns the drum pattern from **e_low 0.057, centroid 33 Hz** into **e_low 0.342, centroid 117 Hz** — audible body on a small speaker — and leaves 2.5 dB of headroom.

## 4. SHARDSTORM — LEAD

```
CRYSTAL  .        CRYSTAL
.        [LEAD]   .
CRYSTAL  .        CRYSTAL
```

- **Techniques:** 5 (sync scream), 7 (pitch-env), ping-pong delay.
- **Intent:** a storm of glass shards — metallic sync lead that scatters.
- **Why the override wins:** CRYSTAL×4 clamps the sync partial to +12 st, an exact 2:1 ratio where hard sync is nearly inaudible (it reads as a plain octave layer). Moving it to +19 st (`oscB_oct` +1, `oscB_semi` +7 = 3:1) restores the sync formant — **centroid 1620 Hz vs 1335 Hz at the mid note, 2924 vs 2301 at the top** — while measuring **DC-free (0.0001)**; the intermediate +7 st reading I first tried measured 0.067 DC. 1/8 ping-pong widens the chord from correlation 0.91 to 0.43.

## 5. FOREST LULLABY — KEYS

```
WOOD     .        .
MOSS     [KEYS]   .
CLOUD    .        .
```

- **Techniques:** 3 (hollow 50 % keys), 11 (lo-fi haze), 13 (cave).
- **Intent:** a warm cassette-tape lullaby piano you can actually play.
- **Why the override wins:** the single biggest playability fix in the book. CLOUD pushes the attack to **310 ms** (unplayable as KEYS) and the fixed 1.1 kHz cutoff makes the top octave **11.0 dB quieter** than the middle (peak −21.6 vs −10.7 dBFS). A 12 ms attack and keytrack 0.5 give **−10.6 / −10.4 / −10.6 dBFS across three octaves** — dead flat — with the tape crush and pitch wobble untouched.

## 6. MIDAS MODE — CHIP

```
.        GOLD     .
GOLD     [CHIP]   GOLD
.        GOLD     .
```

- **Techniques:** 4 (supersquare), ping-pong delay.
- **Intent:** the expensive one — a gold-plated 8-voice supersquare chip lead.
- **Why the override wins:** GOLD×4 buys the width but keeps the CHIP envelope (30 ms decay, 35 ms release), so plain craft is a wide blip with nothing to hold on to. Adding decay/release bloom, a 7-cent detune sheen, dotted-1/8 ping-pong and a keytracked 15 kHz ceiling measures **+177 % spectral motion at the low note** (0.89 vs 0.32) and a **57 % longer decay** (878 vs 559 ms) — it now sustains and shimmers instead of blipping.

## 7. STRATOSPHERE — DRONE (category PAD)

```
CLOUD    CLOUD    CLOUD
CLOUD    [DRONE]  CLOUD
CLOUD    CLOUD    CLOUD
```

- **Techniques:** 13 (cave ambience), 1 (PWM).
- **Intent:** the vast, slow, high-altitude drone.
- **Why the override wins:** CLOUD×8 drives `cave_mix` to 0.95 and the osc level to 0.50 — almost pure reverb with no source left, and the chord render measures **negative L/R correlation (−0.11), i.e. mono-incompatible**. Restoring a dry core (level 0.72, cave 0.55) brings correlation to a healthy **0.48**, and the slowest PWM the LFO can do (8/1 = 16 s at 120 BPM) raises low-note motion **from 0.48 to 0.88**.

## 8. ICICLE HARP — PLUCK

```
GLASS    .        .
.        [PLUCK]  .
.        .        GLASS
```

- **Techniques:** 7 (pitch-env pluck), 2 (thin pulse).
- **Intent:** thin glass icicles struck and left ringing into a dotted delay.
- **Why the override wins:** GLASS×2 gives the 14 % pulse and some delay but leaves `env2_pitch` at 0 — there is no mallet in the attack, so plain craft is a filter blip that dies in 300 ms. Adding the +14 st / 60 ms chirp, a keytracked sweep and dotted-1/8 ping-pong **doubles the ring** (−30 dB decay 668 / 708 / 1038 ms vs 334 / 374 / 289 ms) and **doubles the 2–8 kHz content** (0.22 vs 0.10).

---

# The eight new recipes

## 9. TARPIT — BASS *(new)*

```
.        .        .
.        [BASS]   .
SLIME    SLIME    SLIME
```

- **Techniques:** 14 (glide talk / SLIME wobble → wub).
- **Intent:** a slow, gluey wobble bass that drags like tar.
- **Why the override wins:** SLIME×3 swings the pulse width from 11 % to 99 %, which *gates the level* rather than wobbling the tone, and parks the filter at 650 Hz so nothing moves in the low end. Handing the wobble to LFO2 on the cutoff (tri, 1/8, ~3 octaves around 240 Hz, res 0.70) and the pulse width to the free LFO1 gives **two independent wobbles** and steady motion across the whole range (**0.30 / 0.32 / 0.30 vs 0.23 / 0.06 / 0.04** low→high), plus 1.5 dB headroom.

## 10. THUNDER SEAM — CHIP *(new)*

```
.        .        VOLT
.        [CHIP]   VOLT
.        .        VOLT
```

- **Techniques:** 10 (faked chip arp), 9 (NES metallic), 12 (RAW dirt).
- **Intent:** a lightning seam — the octave-jump chiptune arp the engine has no arpeggiator for.
- **Why the override wins:** VOLT×3 puts a 1/16 sample-and-hold on the cutoff, but CHIP's wide-open 20 kHz leaves nothing to filter, so the jitter is inaudible. Repurposing LFO2 as a square on **pitch** at full depth makes the note jump an octave down and up every 1/16: the fundamental trace measures **267 ↔ 1049 Hz alternating**, against a **flat 524 Hz** for plain craft. RAW aliasing and a short-LFSR layer supply the metal.

## 11. COBBLE THUMP — PERC *(new)*

```
STONE    .        STONE
.        [PERC]   .
STONE    .        STONE
```

- **Techniques:** 8 (chip drums / toms), 12 (RAW).
- **Intent:** a bone-dry aliased cobblestone tom, no reverb anywhere.
- **Why the override wins:** STONE×4 already supplies the dry, RAW, zero-reverb character and a 17 ms release — but not a usable drum, because the inherited PERC pitch envelope sweeps upward and the cutoff collapses to 282 Hz. A descending +24 st / 70 ms drop with a stick click turns the pattern from **e_low 0.041, centroid 54 Hz** into **e_low 0.851, centroid 218 Hz**; keytrack 0.8 makes one recipe cover the whole tom range instead of one note.

## 12. GEYSER — DRONE (category FX) *(new)*

```
.        VOLT     .
.        [DRONE]  .
SAND     SAND     SAND
```

- **Techniques:** 13 (cave ambience) + LFSR texture, used as an FX riser.
- **Intent:** pressure building underground, then a steam eruption — a held riser.
- **Why the override wins:** SAND×3 clamps the noise to 1.0 over a static drone: flat hiss going nowhere. Turning ENV2 into a riser generator (2.4 s attack, sustain 1.0) driving +24 st of pitch and 0.85 of filter envelope up from a closed 260 Hz makes the sound climb for as long as the key is held — the fundamental trace goes **76 → 408 Hz** against plain craft's **flat 64 Hz** — and the CAVE widens it from correlation 0.86 to 0.50.

## 13. HOLLOW GEODE — PLUCK *(new)*

```
.        .        CRYSTAL
.        [PLUCK]  .
CRYSTAL  .        .
```

- **Techniques:** 5 (sync), 7 (pitch-env), bell flavour.
- **Intent:** the anti-diagonal mirror of ICICLE HARP — a struck bell inside a hollow rock.
- **Why the override wins:** CRYSTAL×2 lands the sync partial on +11 st (a sour near-octave) and the 1.6 kHz cutoff filters that partial away entirely. Moving to a clean 3:1 ratio and opening/keytracking the filter makes the metallic partial audible — **2–8 kHz energy 0.56 vs 0.21 at the top note** — and the big damped CAVE gives a **4× longer ring** (1490–1980 ms vs 314–414 ms). Kept deliberately delay-free so it never doubles ICICLE HARP.

## 14. EMBER CROWN — LEAD *(new)*

```
LAVA     LAVA     LAVA
LAVA     [LEAD]   LAVA
.        .        .
```

- **Techniques:** 5 (sync scream), 12 (grind), 14 (legato glide).
- **Intent:** a crown of fire — a screaming, grinding forge lead.
- **Why the override wins:** LAVA×5 crushes to **4 bits at 0.58 mix with resonance climbing toward the screech zone** — two entries off the SOUND_DESIGN anti-pattern list at once. Pulling back to a musical 6-bit 0.40 grind and spending the headroom on a hard-synced 3:1 partial gives **+62 % centroid and 4× the 2–8 kHz energy at the top note** (1433 Hz / 0.375 vs 884 Hz / 0.089): it screams instead of hissing, and legato glide makes it sing.

## 15. AURORA VEIL — PAD *(new)*

```
.        GOLD     .
ICE      [PAD]    ICE
.        GOLD     .
```

- **Techniques:** 4 (supersquare), 1 (PWM).
- **Intent:** the bright golden counterpart to PERMAFROST — shimmering aurora, not ice cave.
- **Why the override wins:** its plain-craft neighbourhood lands almost on top of PERMAFROST's (unison railed at 8, release clamped near 5 s, cave 0.55), so the override's whole job is to drive them apart — and it does: on the same chord, **AURORA VEIL measures centroid 616 Hz and e_hi 0.033 against PERMAFROST's 354 Hz and 0.004**, i.e. roughly twice the brightness and eight times the air, with a dotted-1/4 ping-pong and a short bloom where PERMAFROST has a five-second dark tail.

## 16. SINKHOLE — PAD (category FX) *(new)*

```
OBSIDIAN OBSIDIAN OBSIDIAN
OBSIDIAN [PAD]    OBSIDIAN
OBSIDIAN OBSIDIAN OBSIDIAN
```

- **Techniques:** 13 (cave), inverted filter envelope; the FX drop.
- **Intent:** the floor gives way — hold a chord and everything falls.
- **Why the override wins:** OBSIDIAN×8 clamps oscA to octave −2 and buries the cutoff, so plain craft is a barely-moving rumble (**motion 0.10–0.30**). Putting the oscillator back at pitch and spending ENV2 on a 1.8 s fall — −26 st of pitch with an inverted −0.7 filter envelope closing behind it — measures a fundamental trace of **52 → 28 Hz** against plain craft's **flat 64 Hz**, and **motion 0.99–1.13**: three to ten times the movement of its own neighbourhood.

---

## Coverage

| | |
|---|---|
| Bases | LEAD ×2, BASS ×2, PAD ×3, PLUCK ×2, KEYS ×1, CHIP ×2, PERC ×2, DRONE ×2 |
| Materials | all 14 appear; ICE, LAVA, GOLD, CRYSTAL, SAND, OBSIDIAN, CLOUD ×2, the rest ×1 |
| Categories | LEAD 2, BASS 2, PLUCK 2, PAD 3, KEYS 1, CHIP 2, PERC 2, FX 2 |
| Roles | PWM pad, bright pad, drone, wub bass, crushed bass, kick, tom, sync lead, forge lead, chip arp, supersquare chip, glass pluck, bell pluck, lo-fi keys, riser, drop |

## Pattern shapes (discoverability)

Top row, bottom row, left column, right column, four corners, edge centres,
full ring, main diagonal, anti-diagonal, the "T" (QUARRY KICK), the flanked
pair (MAGMA FLOOR), the arch (EMBER CROWN), the split cross (AURORA VEIL) and
one two-material shape (GEYSER). Every shape a player would plausibly try on
purpose; none is a single cell, and the two two-cell patterns are strict
diagonals (a DICE roll lands one with probability ≈ 7 × 10⁻⁶). All sixteen
base+cells tuples are distinct, so `RecipeBook::match()` order never matters.
