# CHECKPOINT — Phase 6 render pack (STOP: human listening)

Date: 2026-08-02 · **RUN STOPS HERE — Kirill culls the bank by ear.**

## Pipeline state

- 256 candidates authored (`presets/candidates/`, committed): 2× final quotas, every one = craft recipe + minimal overrides + technique note; 16 carry the hidden-recipe grids as showcases; names original, validated against frozen ParamSpec.
- All 256 rendered at 48 kHz / 120 BPM: single note (C4; BASS C2; PERC/PAD/FX C3) + category phrase (PAD/KEYS: held 4-note chord — the distortion-class check; LEAD melody; BASS groove; PERC pattern; CHIP arp; PLUCK run; FX long note).
- Auto-QC (method: RMS-dBFS as LUFS proxy, documented in `_QC_REPORT.csv`): silence < −50 dBFS, softclip-riding (≥0.99 over >0.5% of samples, note OR chord), |DC| > 0.01, NaN/Inf. **6 rejected**, all softclip-riding on the chord phrase (KEYS_12_PUMP_ORGAN, PAD_10_DUSK_TERRACE, PAD_11_BASALT_CHAMBER, PAD_12_ASHGLOW, PAD_20_SLOW_MAGMA, PAD_26_DEEP_CURRENT) — the exact defect class Kirill caught by ear, now caught by machine.
- **250 survivors** normalized to −18 dBFS RMS (peak-limited 0.99) → `CHECKPOINTS/artifacts/renderpack/` (git-ignored, regenerable): `CATEGORY_NN_NAME.wav` + `CATEGORY_NN_NAME_phrase.wav`, 16-bit, 502 files, ~359 MB.
- Survivors per category vs final quota: LEAD 48/24 · BASS 40/20 · PLUCK 32/16 · PAD 27/16 · KEYS 23/12 · CHIP 32/16 · PERC 24/12 · FX 24/12.
- Duplicate flags: coarse time-domain similarity flagged ~30 pairs (column `note` = `DUP? ~ NAME` in `_QC_REPORT.csv`). **Advisory only** — the metric is deliberately coarse and over-flags short plucks; treat as "listen to these back-to-back", not as rejections.

## The cull (Kirill's flight task)

Folder: `CHECKPOINTS/artifacts/renderpack/`
1. Listen by category (Finder: sort by name; space = quick-look plays WAVs offline).
2. `NAME.wav` = the raw tone, `NAME_phrase.wav` = how it sits musically. Judge on both.
3. Collect keepers in a text file `CHECKPOINTS/artifacts/KEEP.txt` — one filename stem per line (e.g. `PAD_03_FROSTBYTE`). Target counts: LEAD 24, BASS 20, PLUCK 16, PAD 16, KEYS 12, CHIP 16, PERC 12, FX 12 = **128**. Over/under is fine — the pipeline trims/backfills and reports.
4. PAD is at 27/16 and KEYS at 23/12 after rejections — still enough headroom to choose well.
5. designer's top-10, if you want a starting point: GEODE VOICE, MAMMOTH STEP, FROSTBYTE, BAMBOO WELL, FIFTH FOUNTAIN, BRINE CLAV, FLINT HAT + OPEN VENT, DRIP GROTTO, TUNDRA CALL, BOW SNAP.

## After the cull (next session)

Paste KEEP.txt (or just say it's in place) → pipeline applies the keep-list exactly, loudness-matches the final bank, replaces the 8 dev presets in `presets/factory/`, verifies browser recipe icons, spell-check pass, lock → qa-runner full battery → CHECKPOINTS/PHASE-6.md.

## Notes

- Rejected 6 stay in `presets/candidates/` untouched — fixable by lowering levels if any name is loved (say so in KEEP.txt with a `!` prefix, e.g. `!PAD_12_ASHGLOW`).
- `_QC_REPORT.csv` has per-candidate numbers; `_REJECTED.txt` the reject reasons.
- Regenerate the whole pack anytime: `python3 <scratchpad>/renderpack.py` (script also committed to `tools/qc/renderpack.py` for permanence).
