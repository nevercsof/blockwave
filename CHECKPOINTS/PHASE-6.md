# CHECKPOINT — Phase 6 (preset factory) + producer feedback batch

Date: 2026-08-03 · Verified by qa-runner (12-item battery) + post-fix ctest. **RUN STOPS HERE for review.**

## Summary

The factory bank is locked: **128 presets** (Kirill's ear-cull of 256 candidates, trimmed 143→128 by documented rules, backfilled to quota), embedded in BinaryData, loudness-matched, chord-safe. Alongside it, the producer's four listening-feedback items were implemented at engine level.

## Producer feedback batch (all four items)

1. **Noise no longer doubles in polyphony.** Root cause was double: every voice ran the same LFSR seed (quantized chords played bit-identical noise, +6 dB coherent), and levels summed. Fix: per-voice seed offsets into the sequence + smoothed 1/√N noise scaling. Measured: 1/4/8 held voices → −13.00/−13.00/−13.01 dB noise band; tonal content sums normally; single-note renders bit-identical to before.
2. **New FX high-pass params** `crush_hp`, `dly_hp`, `cave_hp` (20..2000 Hz log, default 20 = hard bypass, wet-path only — dry bass untouched). Frozen table grew **61→64 (append-only)** — architect ack requested. Transparency proven: default HP leaves every existing golden byte-identical (new golden locks it). TWEAK cells added (rows 2–4 rebalanced to fit — see open questions).
3. **Pluck rumble diagnosed and fixed**: not sub (off in all 21 kept plucks) — CAVE stretching the attack transient into a low bed (8 presets → cave_hp 150–180) and DELAY repeating the attack thump (7 presets → dly_hp 150); BOW_SNAP's down-chirp floor raised 147→196 Hz. Bed −5.4..−7.4 dB, mid-tail loss ≤1.65 dB. 9 plucks clean, untouched. A/B renders: `CHECKPOINTS/artifacts/feedback-batch-1/`.
4. **Bass + reverb HP**: of 18 kept basses only 3 resolve reverb; tuned per preset — TWIN_RAIL 120 Hz, UNDERTOW 100 Hz, STARLESS_POND 120 Hz (wet-tail sub −9 dB, dry body ≤0.35 dB change).
- Perc "abrupt cutoff" report: **player artifact** (Quick Look) — files end in true silence, verified.

## Phase 6 DoD

| DoD item | Status |
|---|---|
| 128 presets shipped meeting category quotas | ✅ LEAD 24 · BASS 20 · PLUCK 16 · PAD 16 · KEYS 12 · CHIP 16 · PERC 12 · FX 12 — exact; embedded (BinaryData reports 128); 0 duplicate names |
| Every preset = recipe + minimal overrides | ✅ override counts: median 3, mean 2.9, max 7 |
| QC report attached | ✅ all 128 render clean (0 silent/NaN/DC, peaks ≤0.9704); loudness: 111/128 within ±1.5 dB of median −21.5 dBFS; **all 52 PAD/KEYS/LEAD pass the 4-note-chord softclip test** (the producer's distortion class); QC CSV in renderpack |
| Kirill approved the bank by ear | ✅ KEEP.txt applied exactly; trims/backfills documented below and awaiting his veto window |

Verified: clean build 277 targets 0 warnings; **337 + 2056 = 2393 checks, 0 failures** (Release and Debug); pluginval s10 SUCCESS VST3+AU (64 params, 128-bank); browser renders 128 with correct folder counts and recipe icons; CPU worst case 3.5 % @ 44.1k/128 (0 % regression); brand grep clean; goldens: only the new fx_wet_default golden added, all existing byte-identical.

## Cull bookkeeping (his 143 → 128)

- Trims — LEAD −7 (GALE_HORN, QUARTZ_SCREAM, WOLF_PINE, PRISM_LANCE, POLAR_CHOIR, MOTH_LANTERN, TIN_COMET), PLUCK −5 (ICE_LATTICE, ZITHER_CREEK, DIAMOND_PLUCK, LILY_POP, PICK_FENCE), PERC −5 (LAVA_STOMP, TREMOR_TOM, TIMBER_TOM, RUBBLE_SNAP, TICK_SHALE). Rules: recipe showcases untouchable → QC dup-pairs resolved first → diversity. Per-trim reasons in the assembly report.
- Backfills BASS +2: **TARPIT** (wub showcase, cutoff-wub niche was missing) and **BRICK_BUMP** (only no-sustain stab bass). **BOULDER_ROLL (his "?") kept** — veto → swap to BASS_16_PIT_STOMP.
- Stale DEV-era tests modernized (agreement test now goes through the recipe book; quota table data-driven; author check updated). The "FLINT HAT silent" scare was the params-only test path — through the real recipe-aware path it renders normally.

## Open questions

1. **Kirill (taste): POWER PELLET** — recognizable Pac-Man term. Legally outside our Mojang-only rule, but flag for trade-dress comfort. Keep or rename (POWER PIP / PIXEL PELLET)?
2. **Kirill: BOULDER_ROLL** stays? (his "?" mark)
3. **Architect ack: frozen table 61→64** (3 appended HP params; nothing renamed/reordered) + **TWEAK rows 2–4 rebalance** (FX cells didn't fit the old gaps; modulators now share row 3, FX chain row 4 in signal order).
4. **Proposal:** `dly_hp` on the two delay-basses (TAUT_WIRE, LANTERN_OIL) — producer's instruction covered reverb only; one word and it's applied.
5. **Proposal:** 17 delicate presets sit below the loudness tolerance, 16 pinned at master_gain +6 ceiling; SPARK_VEIN's true ceiling is the softclip (4.3 max without clipping). Options: accept as "meant to be quiet" (recommended), or a v1.x param-range bump (+12 dB) — cannot be done now, ranges frozen.
6. Recorded: `presets/candidates/` kept in-repo as history (256 JSONs, ~500 KB).

## Artifacts

- Bank: `presets/factory/*.json` (128) · candidates history: `presets/candidates/`
- Renders/QC: `CHECKPOINTS/artifacts/renderpack/` (git-ignored, regenerable) · A/B fixes: `CHECKPOINTS/artifacts/feedback-batch-1/`
- pluginval logs: `CHECKPOINTS/logs/` (2026-08-03) · Screenshots: `CHECKPOINTS/screenshots/` (browser with 128, TWEAK with HP cells)
- Repro: `bash scripts/build.sh` · `cd build/release && ctest` · `scripts/validate.sh CHECKPOINTS/logs`

## Next (после ревью)

Phase 7 — QA/hardening/installers: FL Studio manual checklist on Kirill's Mac, Windows beta round, packaging. Recommended effort: **high**.
