# CHECKPOINT — Phase 2 (parameters & preset system) + approved amendments

Date: 2026-08-02 · Verified by qa-runner. Verdict: **clear for checkpoint**. Per architect instruction this checkpoint does not stop the run — Phase 3 starts immediately; review lands with PHASE-3.

## Summary of work

**Architect amendments (all approved at PHASE-1 review, all implemented):**
1. LFO2→pitch full scale ±12 st with quadratic taper (`amt·|amt|·12`, sign preserved) — verified by render: amt 1.0 → +12.00 st, ±0.5 → ±3.00 st (±0.05 cents).
2. Unison cap 64 → **128 stacks**: 16 voices × 8 unison now real (288 oscillators worst case). New worst-case CPU: **3.5 % @ 44.1k/128, 3.8 % @ 48k/512** (0.22–0.24 %/voice, ~28× headroom).
3. Pitch bend ±2 st, 25 ms smoothed (worst period step 1.74 % vs 12.2 % for a zipper), wired in plugin MIDI and `tools/render` MIDI files.
4. Master softclip ceiling (pulled forward from Phase 5): bit-exact unity below −0.3 dBFS, C1-continuous tanh above, output ≤ 0 dBFS, always on, allocation-free.

**Phase 2 scope:** frozen APVTS of **61 parameter IDs** (exact SPEC IDs/ranges/defaults/tapers; single source of truth `src/ParamSpec.h` generating layout, render mapping, snapshot conversion and preset serialisation); JSON preset save/load per SPEC §Preset format (craft carried opaquely until Phase 4); 8 TEMPORARY dev presets (one per category, SOUND_DESIGN techniques) embedded via BinaryData; lazy user folder `~/Documents/BLOCKWAVE/Presets`; preset browser data model (`plugin/PresetLibrary.h` — category groups, wrap-around next/prev, save/rescan, UI-free interface for Phase 3); full session state save/restore (APVTS + preset meta + craft + formatVersion).

## DoD status

| DoD item | Status |
|---|---|
| Host session state round-trip test | ✅ bit-exact (tolerance 0.0) across all 61 params + preset meta + craft (`tests/StateTests.cpp:420`) |
| Automation of cutoff/PW click-free (smoothing verified by render) | ✅ slew 0.0420 ≤ bound 0.0529; negative control (hard splice) = 0.7354, 14× over bound — test provably catches clicks |
| 8 temporary dev presets load correctly | ✅ all load; 3 rendered: peaks −0.07…−6.30 dBFS, 0 NaN/Inf, 0 overs |
| pluginval s10 | ✅ VST3 + AU SUCCESS, all 61 params fuzzed in both |
| Parameter IDs permanent after this phase | ✅ **61 IDs FROZEN** — all 61 cross-checked against SPEC table incl. tapers and choice sets, zero mismatches |

Tests: `blockwave_tests` 38 → **58 checks**, new `blockwave_state_tests` **184 checks** — **242 green**, zero warnings.

## Notes for the record (qa-runner flags)

- **Golden regeneration:** `tests/golden/hard_sync_A2_48k.{f32,wav}` regenerated — the new master softclip legitimately reshapes peaks above −0.3 dBFS; the old golden actually exceeded 0 dBFS (peak +0.027 dBFS → now −0.060 dBFS), max sample diff 0.00997. LFSR goldens and all other goldens unchanged vs git. Explicitly recorded per golden policy.
- pluginval AU run prints informational "Current program is -1" — harmless (presets exposed via own browser, not AU programs); did not affect SUCCESS.
- DEV_PWM_PAD rides the softclip near ceiling (~1.3 ms runs ≥ 0.999) — matches softclip design; preset-taste call for Phase 6, not a defect.
- Decisions frozen with the IDs: `lfoN_rate` = single float 0.01–40 (whole notes when synced / Hz when free; UI quantises synced display in Phase 3); `dly_time` = 11 choices 1/1…1/32 incl. dotted, default 1/4.

## Artifacts

- pluginval logs: `CHECKPOINTS/logs/` (`...VST3_20260802T030735...txt`, `...AudioUnit_20260802T030752...txt` + console logs)
- Builds: `build/release/BLOCKWAVE_artefacts/Release/{VST3,AU,Standalone}` · CI (push-triggered) supplies Windows VST3
- Repro: `bash scripts/build.sh` · `cd build/release && ctest` · `scripts/validate.sh CHECKPOINTS/logs`

## Open questions / proposals

None new — FX params (crush/dly/cave) are frozen in the APVTS but engine-inert until Phase 5 as planned; host program list stays at 1.
