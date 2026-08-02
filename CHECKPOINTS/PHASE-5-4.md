# CHECKPOINT — Phase 5 (FX block) + Phase 4 (CRAFT grid)

Date: 2026-08-02 · Combined checkpoint per architect instruction (order swapped: FX first, so craft material deltas could be tuned against sounding effects). **RUN STOPS HERE for review.**

Phase 5 was verified and committed separately (commit `23af0cf`, CI green on macOS + Windows); its DoD is restated below for completeness. Phase 4 is the work under review.

---

## Phase 5 — FX block

Signal path per SPEC: per-voice sum → CRUSH → DELAY → CAVE → master gain → softclip.

- **CRUSH** — hold-then-quantize, 1–16 bits, 1–64× downsample, dry/wet.
- **DELAY** — tempo-synced (11 divisions incl. dotted), ping-pong cross-feedback, per-sample delay-time ramp so mid-playback tempo changes glide instead of clicking. Tempo floor 20 BPM (12 s buffer, allocated in `prepare`).
- **CAVE** — 8-line FDN, Householder matrix, per-line damping LP, RT60-derived gains, size-scaled loop lengths, pre-delay + early-reflection tap, DC blocker, denormal flushing. Cavernous, not plate-y.
- **Tail reporting** — honest `getTailLengthSeconds` = delay −60 dB decay + CAVE RT60, capped at 120 s.

### Phase 5 DoD

| DoD item | Status |
|---|---|
| Bypass null tests | ✅ **bit-exact** (max diff 0) at crush/delay/cave/all mix 0 — no golden was altered, proving the bypass |
| Delay locks to host tempo incl. tempo changes | ✅ echo at exactly 24 000 samples (1/4 @ 120 BPM/48k); 120→90 BPM re-locks to 32 000, click-free (slew 0.041 ≤ 0.052; hard-splice control 0.86) |
| Reverb tail survives note-off and transport stop | ✅ rings through note-off *and* panic; RT60 10.01 s @ size 0.9 vs 0.78 s @ 0.2; damp 0.85 drops HF 145 dB vs mids 25 dB |
| FX add ≤ agreed CPU budget | ✅ 3.6 % @ 44.1k/128, 3.9 % @ 48k/512 — FX cost ≈ **0.1 pp** vs pre-FX baseline, budget was ~10.5 % |
| pluginval s10 | ✅ VST3 + AU SUCCESS |

Ear artifacts: `tests/reports/fx_delay_tempo_change.wav`, `tests/reports/fx_cave_tail.wav`.

---

## Phase 4 — CRAFT grid (the killer feature)

### What was built

**Engine (pure C++, bit-deterministic across platforms — only IEEE basic arithmetic, no `pow`/transcendentals):**
- `src/CraftEngine.h` — `craftApply(base, cells[8])`, 8 hand-tuned base archetypes (LEAD/BASS/PAD/PLUCK/KEYS/CHIP/PERC/DRONE), 14 material delta tables, shapeless stacking with diminishing weights (1.0/0.5/0.25/0.125…), fixed-order conflict resolution (STONE's `raw` wins last), positional recipe matching, seeded `craftDice`/`craftMutate`, `autoName()`, FNV-1a snapshot hashing. Frozen cell indexing: `0 1 2 / 3 · 4 / 5 6 7`.
- `src/CraftJson.h` — JUCE adapter: grid↔JSON, `RecipeBook`, `DiscoveryStore` (`~/Documents/BLOCKWAVE/Discoveries.json`, injectable for tests).
- `data/recipes.json` + `docs/RECIPES_FORMAT.md` + `docs/RECIPE_NOTES.md`.
- Craft state is message-thread only; the audio thread sees atomic parameter writes with the existing ~25 ms smoothing. `craft` is no longer opaque: presets apply craft → recipe → param overrides, identically in the plugin and `tools/render`.

**UI (procedural pixel art, zero asset files):**
- `plugin/ui/MaterialArt.*` — original 16×16 sprites for all 14 materials + 8 base glyphs, 12×12 mini-icon renderer, per-editor image cache (no static mutable state).
- `plugin/ui/CraftBlocks.*`, `CraftTab.*` — 3×3 bench with drag-and-drop (cell→cell **swaps**, since position matters), click-cell-then-material fallback in both orders, right-click/DELETE clears, full keyboard navigation, base cycling, 3-word tooltips, big auto-name plate (gold + ★ when a recipe is active), DICE (4 discrete frames, no easing) and MUTATE (name-plate glitch), discovery toast, Discoveries overlay (found = name, unfound = `????`), always-visible n/16 counter read **dynamically** from the recipe book.
- `plugin/ui/KeyStrip.*` — 1.5-octave C3–F4 pixel keyboard, live.
- `plugin/ui/PresetBrowser.*` — the 12×12 slot reserved in Phase 3 now renders each preset's craft as a mini 3×3 icon (craft JSON parsed in `refresh()`, never in `paint()`).

**Processor hooks for the UI (real-time safe):**
- `src/UiMidiQueue.h` — hand-rolled 128-slot SPSC ring (release/acquire), chosen over `juce::AbstractFifo` because it also links into the JUCE-free test suite. `MidiKeyboardState`/`MidiMessageCollector` were rejected: both lock on the audio-thread side. Overflow policy: **drop-newest + release every UI-held note in the same drain**, so a dropped note-off can never stick (600 events into 128 slots → peak 0.0000).
- `src/DiscoveryJingle.h` — C6–G6–C7 square arpeggio, 3 × 90 ms, 5 ms zero-ended fades, −18 dBFS, mixed **after** the master softclip so it is independent of patch, FX and master gain, and re-limited so the sum cannot exceed 0 dBFS. Writes nothing at all when idle — that is what makes the null test exact.
- UI notes drain at sample offset 0 before the host MIDI walk; `uiAllNotesOff` releases only UI-held notes, so host notes survive (verified: host tail RMS 0.40123 identical with and without a UI panic).

### Phase 4 DoD

| DoD item | Status |
|---|---|
| Determinism unit test with golden hashes | ✅ 21 craft golden hashes (8 empty bases, stacked/mixed grids, all 8 spec recipe grids); hashes identical in **Release and Debug** (cross-optimisation determinism). `git status tests/golden/` empty — no silent regeneration |
| Every material audibly distinct on every base (render matrix 14×8) | ✅ **112/112 pairs pass**, worst margin 1.83× (PERC+SAND); 120 WAVs, 0 non-finite, 0 silent. qa-runner's *independent* metric: relative-L2 min 0.097 / median 0.907 / max 2.665, 0 pairs below 0.05 |
| All 16 recipes trigger and sound clearly better than plain craft | ✅ 16/16 parse (on-disk **and** embedded, byte-identical), 16/16 reachable with no shadowing, 0 duplicate patterns, **128/128 near-miss grids correctly reject**, every override changes 13–17 params (evidence below) |
| Discovery toast + jingle work | ✅ toast renders, counter dynamic (0/16 → 1/16 → 4/16 across screenshots); jingle null test **0 diffs** idle, peak exactly −18.00 dBFS, hot patch stays 1.000000000 (≤0 dBFS), decays to silence |

Verified independently by qa-runner on a from-scratch build: **157 targets, 0 warnings**; Release **252 + 328 = 580 checks**, Debug **252 + 328** (the previously reported 250 was stale — the two FX-budget gates now always execute and only the *threshold* varies by config, so no assertion is skipped anywhere); pluginval s10 **SUCCESS on VST3 and AU** with GUI tests included; **0 allocations** across 200 `processBlock` calls with craft + FX + UI MIDI + jingle live; `ScopedNoDenormals` present; **zero mutable static state** in all new sources; CPU **0 % change** vs Phase 5 (see the note below on absolute figures); brand grep clean (all 16 recipe names and all 14 auto-name adjectives original).

Extra sweeps: three recipe patches render finite and non-silent at 44.1/48/96/192 kHz; buffer sizes 1–4096 with a mid-session re-prepare hold C4 at 261.626 Hz.

**Distinctness metric** (four measures vs the bare base, union covers every delta class): relative RMS, spectral-centroid shift in octaves, cosine distance of √-compressed spectra, post-note-off tail RMS delta. Thresholds 0.06 / 0.06 oct / 0.04 / 0.30.

**Recipe-beats-plain-craft evidence** (recipe-ON vs the identical grid with the override disabled):
- FOREST LULLABY — plain craft is 11.0 dB quieter three octaves up (−21.6 vs −10.7 dBFS); recipe holds −10.6/−10.4/−10.6 across three octaves; attack 310 ms → 12 ms.
- THUNDER SEAM — fundamental alternates **267 ↔ 1049 Hz** (the arp the engine has no arpeggiator for); plain craft sits flat at 524 Hz.
- GEYSER — fundamental rises **76 → 408 Hz**; plain craft flat at 64 Hz.
- SINKHOLE — falls **52 → 28 Hz**, centroid motion 0.99–1.13 vs 0.10–0.30.
- COBBLE THUMP — low-band energy **0.041 → 0.851**, centroid 54 → 218 Hz. QUARRY KICK — 0.057 → 0.342.
- STRATOSPHERE — chord L/R correlation **−0.11 (mono-incompatible) → 0.48**.
- HOLLOW GEODE — 2–8 kHz 0.21 → 0.56, ring 314–414 ms → 1490–1980 ms.
- AURORA VEIL vs PERMAFROST on the same chord: centroid 616 vs 354 Hz — the two PAD recipes are measurably not redundant.
- Positional payoff: PERMAFROST differs from the shapeless PAD+ICE×3 grid by 26.5 % RMS.

### The 16 recipes

Spec eight (patterns frozen, overrides hand-tuned): **PERMAFROST** (PAD, ICE top row), **MAGMA FLOOR** (BASS, OBSIDIAN L+R, LAVA below), **QUARRY KICK** (PERC, TNT top+bottom, SAND left), **SHARDSTORM** (LEAD, CRYSTAL corners), **FOREST LULLABY** (KEYS, WOOD/MOSS/CLOUD column), **MIDAS MODE** (CHIP, GOLD edge-centres), **STRATOSPHERE** (DRONE, CLOUD full ring), **ICICLE HARP** (PLUCK, GLASS diagonal).

New eight: **TARPIT** (BASS, SLIME bottom row — the wub), **THUNDER SEAM** (CHIP, VOLT right column — faked arp), **COBBLE THUMP** (PERC, STONE corners — dry tom), **GEYSER** (DRONE, VOLT + SAND — FX riser), **HOLLOW GEODE** (PLUCK, CRYSTAL anti-diagonal — sync bell), **EMBER CROWN** (LEAD, LAVA arch — sync scream), **AURORA VEIL** (PAD, GOLD + ICE split cross), **SINKHOLE** (PAD, OBSIDIAN ring — FX drop).

Full patterns, techniques and rationale: `docs/RECIPE_NOTES.md`.

---

## Open questions (numbered, for the architect)

1. **`env2_pitch` sign contradicts the sound-design bible — docs are wrong, not the engine.** `Voice.h:213` computes `pitchMod = env2_pitch · e2` with `e2` rising 0→peak→decay, so a **positive** value starts the pitch high and settles it to the note — the classic kick/pluck drop. Technique 7 (pluck, `+7..+24`) matches the engine. Technique 8 (kick, `−24..−36`) and CRAFT_GRID's TNT delta (`−24 st`) are inverted: as written they dive down and rise back — a "boop", not a "boom". Inverting the engine would break the pluck, so the fix belongs in the docs + the TNT/PERC tables. **This changes golden hashes**, so it needs an explicit go-ahead. preset-designer already worked around it (both drum recipes override to positive).
2. **Short-mode LFSR carries DC** — measured 0.0155 at `noise_level` 0.12 sustained (~13 % of noise amplitude); long mode is DC-free. Phase 6 auto-QC rejects DC offset, so this will bite the preset pipeline. Balance the short sequence, or DC-block the noise path?
3. **`oscB_semi` (±12) cannot reach a real sync formant** — +12 is an exact 2:1 and inaudible as sync; three recipes needed `oscB_oct` to compensate. Should CRYSTAL's delta add `oscB_oct +1` rather than semitones only?
4. **Material stacking hits the rails at 3+ copies** (ICE×3, OBSIDIAN×2, TNT×2, SAND×3, CLOUD×8 all clamp). Proposal: steeper copy weights or soft-knee clamping so copies 3–8 still do something musical.
5. **GOLD's "master sheen (HP rumble cut)" was dropped** — no parameter in the frozen SPEC table can express it. Accept the loss, or add a parameter in a later version (IDs are frozen, so this cannot be retrofitted into v1)?
6. **Two spec recipe patterns were ambiguous** and are now frozen by our reading: FOREST LULLABY = left column (cells 0/3/5); ICICLE HARP = GLASS at cells 0 and 7 (the base occupies the diagonal's centre). Documented in `docs/RECIPES_FORMAT.md` — please ack explicitly, since changing them later breaks discovered-recipe compatibility.
7. **`master_gain` trims inside three recipes** (MAGMA FLOOR −2.0, TARPIT −2.5, EMBER CROWN −2.5 dB) because their plain bases already peak at 0 dBFS on a single note. Keep, or let Phase 6 loudness-matching own all level trims?

### Defects found beyond the DoD list — both FIXED and independently verified

8. **✅ FIXED — audible click on grid change (CRAFT_GRID.md §4).** Root cause was not where it looked: `PAD + OBSIDIAN×8` **is the SINKHOLE recipe**, whose hand-tuned override pins `env2_s = 1.0` alongside `filt_env = −0.7`. `Voice::updateControls` read `p.filt_env` **raw** while the neighbouring `cutoffLog2` was smoothed, so swapping base to BASS (`filt_env +0.25`) or BASS+LAVA (`+0.65`) moved the ENV2→cutoff term from −3.5 to +3.25 octaves in a single 16-sample control step — a ~6.75-octave instantaneous cutoff leap that flung the filter open for one step and passed raw polyBLEP square edges straight through. That is why only the *combination* failed: bare PAD has `filt_env = 0`, and OBSIDIAN×8's octave shift alone is phase-continuous. A second, smaller instance of the same class was found and fixed in `AdsrEnv::tick`, whose sustain stage assigned `level = sustain` outright whenever a patch change moved sustain under a held note.
   **Fix** (targeted, no blanket smoothing): `filt_env` joined the existing ~25 ms one-pole smoother set (`src/BlockwaveEngine.h`, published via `ModContext::filtEnv`), `src/Voice.h` uses the smoothed value, and the ADSR sustain stage glides at the decay coefficient. **Goldens unchanged** — craft output is bit-identical, corroborated by the same 21 craft hashes passing with the fix both applied and reverted. **CPU cost 0 %** (one float multiply-add per control chunk).
   **Measured** (qa-runner's *own* harness, different swap instants and two detectors): `→ BASS` **5.56 → 1.87**, `→ BASS+LAVA` **12.50 → 2.05** — inside the clean-control band, with controls unchanged to two decimals. Holds at 44.1/48/96/192 kHz × blocks 16–4096. Regression test proven to bite: reverting the fix fails 8 assertions at exactly the pre-fix ratios.
   **Lesson worth keeping:** the defect exists *only* through `craftSnapshotWithRecipes`. Measured through raw `craftApply` both regressions score 1.06/1.24 pre-fix — any future click probe on the non-recipe path reports a false pass.
9. **✅ FIXED — recipes 9–16 now covered.** New `tests/CraftCoverageTests.h` is fully data-driven over `book.getNumRecipes()` (no hardcoded count, so recipe updates need no test edits): disk and BinaryData parse and are byte-identical (17 307 bytes), disk/BinaryData/processor counts and per-recipe name+pattern+params all agree, zero duplicate patterns, every recipe reachable via `match` with no first-hit shadowing, all **128 single-cell near-misses correctly rejected**, and every override provably changes the craft result.

**Totals after the fix: 854 checks** (252 render + 602 state), 0 failures in Release *and* Debug; pluginval s10 SUCCESS on VST3 + AU with GUI tests; 0 allocations on the audio thread; goldens byte-identical.

### Notes on repository hygiene (decided, not asking)

- **The 66 MB craft matrix is `.gitignore`d, not committed.** 120 float WAVs would sit in a public GPLv3 repo forever and cannot be removed cheaply. It is fully deterministic and regenerable in one documented command, so it stays local and is referenced by path. Say the word if you want a compressed copy committed instead.
- The combined `PHASE-5-4.md` filename deviates from CLAUDE.md's `PHASE-N.md`; it is the architect's explicit instruction, recorded here so the deviation is traceable. Phase 5 has no standalone checkpoint file — its DoD is restated above.
- `craft_toast_detail_1x.png` was cropping the DICE/MUTATE row instead of the toast; crop rect fixed in `tools/screenshots/ScreenshotsMain.cpp`.
- **CPU figures are baseline-sensitive.** The worst-case patch measured 3.6 % @ 44.1k/128 and 3.9 % @ 48k/512 earlier in the session, and **4.9 % / 5.3 %** on the same machine later. qa-runner settled it by measuring pre-fix and post-fix back to back: identical, so the gap is machine load, not a regression. **Future comparisons must measure both sides in the same session** rather than against a number recorded on another day. Either way the headroom is large.
- **Advisory for whoever extends the click test above 48 kHz:** `craft_transition_click_free` uses a fixed 2.5× ratio gate that does not generalize — at 192 kHz the fixed transition scores 3.80 post-fix purely because the normalizing denominator halves with rate, while the absolute discontinuity stays ~0.017 (−35 dBFS), *smaller* than a clean control's step at the same rate. Needs a rate-aware bound or an absolute floor before use above 48 kHz.
- qa-runner's harness left a real `~/Documents/BLOCKWAVE/Discoveries.json` containing PERMAFROST and SINKHOLE; deleted, so the counter is a virgin 0/16 for demos. The shipped tests isolate the store correctly.

## Scope flags (built but not in SPEC — say the word and they go)

1. **CLEAR button** on the bench (empties the 8 material cells, keeps the base) — convenience.
2. **Cell→cell drag swaps** rather than move-and-empty — more useful when hunting position-sensitive recipes.
3. Per-recipe `note` field in `data/recipes.json` (ignored by the parser, same tolerance as the existing top-level `comment`).

## Proposals (not built)

1. Show the found recipe's 3×3 pattern on the Discoveries page — a sharing aid, but it is a hint surface, so it is the architect's call.
2. Discoveries page currently an overlay, not an always-visible panel (16 slots do not fit beside the bench); the n/16 counter is always visible as SPEC requires.
3. A sample-rate/buffer change mid-jingle cuts it at a non-zero sample (theoretical click). Audio is stopped across `prepareToPlay` anyway, so this matches existing behaviour.

## Artifacts

- Screenshots (15): `CHECKPOINTS/screenshots/` — regenerate: `./build/release/blockwave_screenshots_artefacts/Release/blockwave_screenshots CHECKPOINTS/screenshots`
- Craft matrix (120 WAVs, local only): regenerate: `./build/release/render_artefacts/Release/render --craft-matrix CHECKPOINTS/artifacts/craft_matrix --sr 48000`
- pluginval s10 logs: `CHECKPOINTS/logs/` (`...VST3_20260802T112825...txt`, `...AudioUnit_20260802T112843...txt` + console logs)
- FX ear artifacts: `tests/reports/fx_delay_tempo_change.wav`, `tests/reports/fx_cave_tail.wav`
- Repro: `bash scripts/build.sh` · `cd build/release && ctest --output-on-failure` · `scripts/validate.sh CHECKPOINTS/logs`
- Recipe docs: `docs/RECIPE_NOTES.md` (patterns + rationale), `docs/RECIPES_FORMAT.md` (schema, frozen cell indexing)

## Human actions for this review

0. **Windows CI — ✅ already green** on commit `d5cd113`: both `macos-latest` and `windows-latest` passed, MSVC `/W4 /WX` build clean and Windows pluginval s10 SUCCESS. Nothing to check manually.
1. **Look at the CRAFT screenshots** — taste verdict on the bench, material art, toast and keyboard.
2. **Play it**: the AU is installed at `~/Library/Audio/Plug-Ins/Components/BLOCKWAVE.component`; drag blocks, hit DICE, try to find a recipe by ear.
2a. **One perceptual confirmation qa-runner cannot close automatically** — "no clicks" is ultimately an ears judgement. In FL Studio: load BLOCKWAVE, open CRAFT, set base **PAD** and drag **OBSIDIAN** into all 8 cells (the name should turn into SINKHOLE). Hold a sustained C3 (or loop a 4-bar C3), and **while it is still sounding** switch the base from PAD to **BASS**; repeat with one **LAVA** in cell 1. Expected: a smooth ~25 ms glide with no filter pop or zip. Report anything audible.
3. **Listen to the craft matrix** if you want to sanity-check materials: `CHECKPOINTS/artifacts/craft_matrix/` (`BASE.wav` vs `BASE_MATERIAL.wav`).
4. Architect: answer the 9 open questions above — #1 and #6 are the ones that get expensive to change later.
