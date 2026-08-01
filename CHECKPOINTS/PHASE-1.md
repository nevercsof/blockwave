# CHECKPOINT — Phase 0 + Phase 1

Date: 2026-08-01 · Machine: macOS (Apple silicon), clang 17, JUCE 8.0.8, CMake 4.4.2.
Verified by qa-runner (independent re-run of every claim). Verdict: **clear for checkpoint**.

## Summary of work

**Phase 0:** repo layout (`src/ plugin/ tools/ presets/ art/ tests/`), JUCE 8.0.8 via FetchContent, CMake presets (release/debug/win-release), plugin shell builds VST3 + AU + Standalone, `scripts/build.sh` (one-command clean build) + `scripts/validate.sh` (pluginval s10, installs AU into the user Components dir first — macOS registry requirement), GitHub Actions macOS+Windows matrix with pluginval s10 and artifact upload, GPLv3 LICENSE + headers, `.gitignore`, `THIRDPARTY.md`, README.

**Phase 1:** pure C++ engine in `src/` (no plugin-layer dependency): polyBLEP pulse osc A/B/SUB (4-point cubic-B-spline BLEP correcting both edges **and** the hard-sync reset), PW 1–99%, RAW bypass; NES 15-bit LFSR noise (long/short, fixed ≈33144 Hz clock); TPT/cytomic SVF (LP24/LP12/BP/HP); 2× exponential ADSR; 2× LFO with tempo sync; voice manager (poly 1–16 / mono / legato, oldest-quietest stealing, glide always/legato in log-freq, velocity→amp); unison 1–8 with detune/spread; 25 ms parameter smoothers; seqlock ParamStore (fields mirror SPEC IDs 1:1 for Phase 2). Plugin wired: sample-accurate MIDI → sound, host tempo, mono/stereo, chunking for oversized host blocks. `tools/render` CLI (`render preset.json <input.mid|note:C4:2s> out.wav [--sr N] [--bpm N]`). Test suite `blockwave_tests` (ctest-integrated), 38 checks.

## DoD status

### Phase 0
| DoD item | Status |
|---|---|
| Clean build from scratch, one documented command (macOS) | ✅ `scripts/build.sh --clean`, exit 0, zero warnings from our sources |
| CI green with Windows VST3 artifact downloadable | ⏸ **pending push** — workflow ready (`.github/workflows/ci.yml`), repo not on GitHub yet (needs `gh auth login` — Kirill's OAuth click) |
| pluginval strictness 10 on empty shell, both platforms | ✅ macOS VST3+AU (logs below); Windows pending same push |
| Standalone launches, passes silence without crackles | ✅ launches/quits cleanly (automated); **audible check = Kirill, manual** |

### Phase 1
| DoD item | Status |
|---|---|
| Pitch accuracy ±1 cent | ✅ worst error **+0.038 cents** (A3 @ 44.1k), 12 sr×note combos |
| Aliasing report: naive vs polyBLEP @ C7 | ✅ RAW −24.19 dBc vs polyBLEP **−50.17 dBc** (Δ 25.98 dB) — `tests/reports/aliasing_report.txt` |
| Sync/noise/env golden renders | ✅ bit-exact vs `tests/golden/` (LFSR periods 32767 / 93; ADSR shape verified) |
| No allocations on audio thread (debug allocator assert) | ✅ operator-new guard: 0 allocations across 201 blocks incl. note-ons and stealing (active in Debug and Release tests) |
| pluginval s10 | ✅ VST3 + AU SUCCESS on the sounding plugin |
| CPU report, 16-voice × 8-unison worst case @ 44.1k/128 | ✅ see below |

Extra verified: block-size invariance 16–4096 bit-identical; renders sane at 44.1/48/96/192k (RMS 0.286, peaks ≤0.542, no NaN/clip).

## CPU (baseline for future checkpoints)

- Requested worst case 16 voices × 8 unison → engine caps at **64 unison stacks total** (`src/BlockwaveEngine.h`), i.e. 16×4: **2.2 %** of one core @ 44.1k/128; 2.2 % @ 48k/512.
- Full unison 8 voices × 8: 1.7 % @ 44.1k/128.
- Cap rationale: bounded worst case (128 pulse + 16 sub + 16 noise oscillators), full 8-way unison preserved up to 8 voices.

## Artifact paths

- Build: `build/release/BLOCKWAVE_artefacts/Release/{VST3/BLOCKWAVE.vst3, AU/BLOCKWAVE.component, Standalone/BLOCKWAVE.app}` (AU also installed at `~/Library/Audio/Plug-Ins/Components/`)
- pluginval s10 logs: `CHECKPOINTS/logs/` (timestamped VST3 + AudioUnit + console logs)
- Audio: `CHECKPOINTS/artifacts/c4_{44k,48k,96k,192k}.wav` (default patch, C4 2 s + tail); golden WAVs in `tests/golden/`; aliasing numbers in `tests/reports/aliasing_report.txt`
- Repro commands: `bash scripts/build.sh` · `./build/release/blockwave_tests` · `scripts/validate.sh CHECKPOINTS/logs`

## Open questions

1. **Mod depth scalings** not fixed by SPEC — dsp-engineer chose: LFO2→pitch ±2 st, LFO2→cutoff ±4 oct, ENV2→cutoff ±5 oct at full `filt_env`, LFO2→vol ±50 %. Approve or retune before Phase 2 freezes parameter semantics?
2. **LFO1 shape** — SPEC defines `lfo2_shape` but not LFO1's; implemented as fixed triangle (classic PWM). Confirm.
3. **Unison cap 64 stacks** (16 voices → max 4-way unison) — acceptable, or prefer a different budget?
4. **Pitch bend** is not in the SPEC parameter table and is not implemented — v1 feature or proposal?
5. Golden files were generated and verified within this same session (repo had no prior goldens) — they lock behavior from now on; flagging provenance per qa-runner.

## Proposals (out of scope, not built)

1. Master softclip is Phase 5; until then resonant patches can peak slightly >1.0 into the host (measured 1.01). Could pull the softclip forward to Phase 2 if it bothers testing.
2. `note:C4:2s` input spec in `tools/render` (implemented as a test convenience; documenting as permanent CLI feature).

## Human actions needed

1. `gh auth login --web --git-protocol https` → I create the public repo, push, and confirm CI green + Windows artifact (closes the last Phase 0 DoD line).
2. Listen: standalone silence check (~30 s, buffers 128 and 1024) and the `CHECKPOINTS/artifacts/c4_*.wav` renders.
3. Answer open questions 1–4 (architect can take these).
