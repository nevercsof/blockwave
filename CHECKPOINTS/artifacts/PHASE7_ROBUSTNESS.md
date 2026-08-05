# PHASE 7 — automated robustness sweeps (qa-runner)

Scope: the automated half of Phase 7. The FL Studio manual checklist is written
separately and is **not** covered here.

| | |
|---|---|
| Commit under test | `54e3011` (no production code changed by this work) |
| Platform | macOS 15 (Darwin 25.1.0), arm64, clang, Release `-O3 -Wall -Wextra -Werror` |
| Suites | `blockwave_tests` (engine, JUCE-free) + `blockwave_state_tests` (JUCE/processor) |
| Checks before this work | 3024 (612 + 2412), 0 failures |
| Checks after this work | **4709 (843 + 3866), 0 failures** — +1685 new checks |
| Suite wall time | 22.5 s + 6.4 s (was 1.3 s + 1.8 s) |
| pluginval | not re-run — no production code changed (per brief) |

New test files (not committed): `tests/RobustnessTests.h`,
`tests/RobustnessProcTests.h`, plus two include/call lines each in
`tests/TestMain.cpp` and `tests/StateTests.cpp`.

## Reproduce everything

```bash
cd /Users/kirillboyko/Downloads/blockwave-kickoff
bash scripts/build.sh                       # Release, /W4-equivalent clean
build/release/blockwave_tests                                    # engine sweeps
build/release/blockwave_state_tests_artefacts/Release/blockwave_state_tests
cd build/release && ctest --output-on-failure                    # both, 29 s
```

Out-of-suite sweep over the whole factory bank (768 renders through the
documented CLI, ~40 s):

```bash
python3 <scratch>/sweep128.py 44100     # and 48000 88200 96000 176400 192000
# per-preset: render presets/factory/NAME.json note:A3:1.0s out.wav --sr N
```

---

## Verdict per sweep

| # | Sweep | Result | Key numbers |
|---|-------|--------|-------------|
| 1 | Sample rates 44.1/48/88.2/96/176.4/192 k | **PASS** | 8 presets × 6 rates in-suite + 128 presets × 6 rates out-of-suite; 0 non-finite, peak max 0.9461, pitch median 1.78 ¢ |
| 2 | Buffer sizes 16…4096 + pathological | **PASS** | 13 sizes + 5 pathological sequences, max \|diff\| = 0 (bit-identical) |
| 3 | Offline vs plugin path | **PASS** | identical-parameter deviation **0** at 4 block sizes; preset-mapping deviation −54.1 dBFS (finding F2) |
| 4 | Tempo changes | **PASS** | 14 BPM locks, error 0 samples; 7 abrupt jumps re-lock, error 0 samples; worst click ratio 2.80 vs splice 20.4 (finding F1) |
| 5 | 128 presets switched under a held chord | **PASS** | 0 non-finite, peak ≤ 1.0, **0 audio-thread allocations**, still sounding after |
| 6 | 8 concurrent instances | **PASS** | bit-identical to running each alone (deviation 0) at 6 rates / 8 block sizes |
| 7 | Extreme parameters + fuzz | **PASS** | 134 rails + 300 seeded random states, 0 non-finite, peak max 0.5854, no stall |
| 8 | CPU under stress | **PASS** | 3.5 % @ 44.1k/128 … 14.6 % @ 192k; FX budget gate (10.5 %) holds |

**Findings: 4 (none blocking).** F1 and F4 need an architect/dsp-engineer
decision; F2 and F3 are characterised and closed.

---

## 1. Sample-rate sweep

### 1a. Pitch is rate independent (`[sr_pitch_sweep]`, engine suite)

Default square, notes A1…A6, zero-crossing f0, gate ±1 cent.

| Sample rate | worst pitch error over A1…A6 |
|---|---|
| 44 100 | +0.038 ¢ |
| 48 000 | +0.034 ¢ |
| 88 200 | +0.019 ¢ |
| 96 000 | +0.018 ¢ |
| 176 400 | +0.010 ¢ |
| 192 000 | +0.009 ¢ |

88.2 k and 176.4 k are in the matrix deliberately — an engine hard-wired to
44100/48000 constants passes 96/192 k by luck and fails these.

### 1b. Heavy patch at every rate (`[sr_stress_patch]`)

All sources + both pulses hard-synced + all FX + all six wet-path filters:

| Rate | peak | rms | non-finite |
|---|---|---|---|
| 44 100 | 0.3022 | 0.0500 | 0 |
| 48 000 | 0.3006 | 0.0500 | 0 |
| 88 200 | 0.3023 | 0.0500 | 0 |
| 96 000 | 0.3018 | 0.0500 | 0 |
| 176 400 | 0.3021 | 0.0499 | 0 |
| 192 000 | 0.3018 | 0.0499 | 0 |

### 1c. Eight factory presets × six rates (`[preset_sample_rate_sweep]`, state suite)

One preset per SPEC category; five are recipe showcases; PERC included.
Assertions per render: finite, peak ∈ (0.001, 0.98), and f0 within ±25 ¢ of the
48 kHz reference when the preset is tonal (autocorrelation confidence > 0.80).

| Preset (category) | 48 k f0 | tonal | peak range over 6 rates | worst cents vs 48 k |
|---|---|---|---|---|
| SHARDSTORM (LEAD, recipe) | 219.18 Hz | yes (0.99) | 0.4084 – 0.4177 | +4.0 |
| MAGMA FLOOR (BASS, recipe) | 109.34 Hz | yes (0.97) | 0.3338 – 0.3340 | +1.4 |
| QUARRY KICK (PERC, recipe) | — | no (0.64) | 0.1983 – 0.2322 | n/a |
| PERMAFROST (PAD, recipe) | 219.18 Hz | yes (0.99) | 0.4114 – 0.4348 | +2.0 |
| FOREST LULLABY (KEYS, recipe) | 220.18 Hz | yes (0.99) | 0.3733 – 0.3734 | −6.1 |
| COIN CHUTE (CHIP) | — | no (0.50) | 0.6767 – 0.6790 | n/a |
| KALIMBA COVE (PLUCK) | — | no (0.70) | 0.4047 – 0.4049 | n/a |
| STATIC FIELD (FX) | 110.09 Hz | yes (0.90) | 0.3045 – 0.3170 | −1.8 |

Worst \|DC\| over all 48 renders: 0.0008.

### 1d. Whole factory bank × six rates (out of suite, 768 renders)

Rendered through `tools/render` (the offline path) at `note:A3:1.0s`:

| Rate | peak max | peak min | peak mean | rms mean | \|dc\| max | non-finite | presets ≥ 0.98 |
|---|---|---|---|---|---|---|---|
| 44 100 | 0.9461 | 0.0315 | 0.3671 | 0.0522 | 0.01192 | 0 | 0 |
| 48 000 | 0.9449 | 0.0310 | 0.3665 | 0.0522 | 0.01192 | 0 | 0 |
| 88 200 | 0.9436 | 0.0262 | 0.3660 | 0.0522 | 0.01193 | 0 | 0 |
| 96 000 | 0.9432 | 0.0253 | 0.3661 | 0.0522 | 0.01193 | 0 | 0 |
| 176 400 | 0.9443 | 0.0214 | 0.3660 | 0.0520 | 0.01191 | 0 | 0 |
| 192 000 | 0.9443 | 0.0212 | 0.3660 | 0.0519 | 0.01191 | 0 | 0 |

Loudest preset at every rate: SPARK VEIN (0.9432–0.9461, always < 0.98).
Pitch across 97 tonal presets × 6 rates (582 measurements): median 1.78 ¢,
p95 3.99 ¢. One 5039 ¢ outlier (DEEP SHAFT @ 176.4 k) is an **octave error of
the throwaway numpy estimator**, not the synth: that preset reads 109.16 /
109.09 / 109.16 / 109.22 / **2004.55** / 109.15 Hz with byte-identical peak and
rms at every rate. The in-suite estimator does not reproduce it.

Largest level drift across the rate matrix (all with constant rms except the
last, see finding F4):

| Preset | 44.1 k → 192 k peak | note |
|---|---|---|
| FLINT HAT | 0.700 → 0.851 (+1.7 dB) | short-LFSR hat, HP 3.2 kHz; rms constant at 0.0145 — crest factor only |
| NARROW GATE | 0.481 → 0.372 (−2.2 dB) | 12.5 % duty pulse; rms constant at 0.0777 — polyBLEP overshoot at 44.1 k |
| CAVE MOUTH | 0.0315 → 0.0212 (−3.4 dB) | **rms also drops 1.33× — finding F4 (CAVE reverb)** |

---

## 2. Buffer-size sweep

`[buffer_size_sweep]` — heavy patch, everything compared against the 512-sample
reference render, at 48 kHz:

| Block | 16 | 32 | 64 | 128 | 256 | 512 | 1024 | 2048 | 4096 |
|---|---|---|---|---|---|---|---|---|---|
| max \|diff\| | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

Repeated at the ends of the rate range: 44.1 k block 16 / 4096 → 0; 192 k block
16 / 4096 → 0.

`[pathological_block_sequence]` — cycling block-size sequences a sane host never
produces, all compared against the same 512 reference:

| # | Sequence | max \|diff\| |
|---|---|---|
| 0 | 1, 4096, 3, 2048, 7, 1024, 13, 512, 17, 256, 31, 128, 61, 64, 127, 32, 251, 16 | 0 |
| 1 | 1 (one sample per call, whole render) | 0 |
| 2 | 4096, 1 | 0 |
| 3 | 15, 17, 16, 1, 33, 47, 4095, 2, 2049 | 0 |
| 4 | 3, 5, 7, 11, 13, 17, 19, 23, 29, 31 | 0 |

This is beyond the pre-existing `test_block_size_invariance` (16/61/128/1024/4096
at 48 k only): 9 sizes + 2 extra rates + 5 pathological sequences.

`[mid_session_reprepare]` — seven consecutive `prepare()` calls with the host
changing both rate and block size mid-session
(44.1k/64 → 48k/1024 → 96k/16 → 192k/4096 → 88.2k/128 → 176.4k/512 → 48k/512),
notes held across each: 0 non-finite, peak 0.3375, and A4 measures 440.010 Hz
(+0.040 ¢) after the last re-prepare.

---

## 3. Offline (faster-than-realtime) vs plugin path

`[offline_vs_plugin_path]` — two separate claims, four presets × four host block
sizes (64/128/512/1024):

| Claim | Result |
|---|---|
| (a) same resolved parameters: `tools/render` loop vs `processBlock` | **max \|diff\| = 0** — bit-identical, all 16 combinations |
| (b) preset → parameters, render mapping vs plugin APVTS | worst audio deviation 0.00197 = **−54.1 dBFS**, on STATIC FIELD only; 0 on the other three |

The pre-existing parameter-level agreement test (`[render_plugin_agreement]`,
129 presets) still reports its worst relative deviation unchanged and passes.

**Finding F2 (closed, informational).** Claim (b) is not bit-exact because
`loadPresetVar` runs one more float round trip than `tools/render`:
plain → normalised → (host stores float) → plain, versus `plainFromVar`'s single
round trip. Example, STATIC FIELD: `filt_cutoff` is 900 Hz in the renderer and
899.999695 Hz in the plugin (3.4e-7 relative); nine parameters differ by
≤ 1.2e-7 relative. On chaotic patches (LFSR noise + S&H LFO + resonance) that
seed difference diverges into a −54 dBFS waveform difference over 2 s; on the
other three presets it stays exactly 0. No action recommended — the deviation is
40 dB below the quietest factory preset and both paths are internally
deterministic. Test asserts (a) at zero and bounds (b) at −40 dBFS.

---

## 4. Tempo changes

### 4a. Echo lock across 14 BPMs (`[tempo_bpm_lock_sweep]`)

1/4-note synced delay, first-echo lag measured by cross-correlation, expected
`60/BPM × sr`, gate ±2 samples:

60, 70, 80, 90, 100, 110, 120, 128, 140, 150, 160, 174, 186, 200 BPM →
**worst error 0 samples.**

### 4b. Re-lock after abrupt jumps (`[tempo_relock_sweep]`)

Tempo jumps from 120 BPM at t = 1.2 s; settled echo spacing read off the tail by
normalised autocorrelation over t ∈ [3, 6] s:

| Target BPM | 60 | 75 | 90 | 100 | 140 | 160 | 200 |
|---|---|---|---|---|---|---|---|
| measured spacing (samples) | 48000 | 38400 | 32000 | 28800 | 20571 | 18000 | 14400 |
| expected | 48000 | 38400 | 32000 | 28800 | 20571 | 18000 | 14400 |
| error | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

### 4c. Click behaviour on abrupt jumps (`[tempo_jump_click_free]`)

Worst single-sample step in the 30 ms after the jump, against 1.25× the steady
step before and after (the project's craft-transition metric):

| Jump | step | bound | ratio |
|---|---|---|---|
| 120 → 60 | 0.0217 | 0.0286 | 0.76 |
| **60 → 200** | **0.0643** | **0.0230** | **2.80** |
| 200 → 60 | 0.0374 | 0.0296 | 1.26 |
| 120 → 200 | 0.0294 | 0.0231 | 1.27 |
| 120 → 121 | 0.0151 | 0.0190 | 0.79 |
| 174.3 → 91.7 | 0.0108 | 0.0244 | 0.44 |
| 90 → 180 | 0.0418 | 0.0208 | 2.01 |
| 180 → 90 | 0.0160 | 0.0256 | 0.62 |
| negative control (hard splice) 60 → 200 | — | — | **20.41** |
| negative control (hard splice) 120 → 60 | — | — | **20.65** |

Gate in the test: 4.0, documented in `tests/RobustnessTests.h`.

**Finding F1 (open — architect/dsp-engineer decision).** Large tempo *increases*
score above the 2.5 ratio the craft matrix uses. Cause is structural, not a bug
in the smoother: shortening the delay time glides the read pointer to the new
tap over ~25 ms, so the line replays faster — a bounded tape-style pitch-up
"zip". 60 → 200 BPM on a 1/8 tap moves 0.5 s → 0.15 s in 25 ms, i.e. ~14×
playback speed for that instant, and scores 2.80. It is not a discontinuity: the
hard-splice control (what an unsmoothed jump does) scores 20.4 on the same
metric, 7× higher.
**Question Q1 for the architect:** is the zip acceptable (it is standard
time-domain-delay behaviour and arguably musical), or should the delay
crossfade between taps on a tempo change? Only reachable by abrupt host tempo
jumps with delay engaged — smooth tempo automation never triggers it.

### 4d. Long tempo ladder (`[tempo_ladder_stability]`)

33 changes (60 → 200 → 60 in 10 BPM steps, then 33 BPM, 999 BPM, 120 BPM) with a
note held, delay feedback 0.85 and reverb on: 0 non-finite, peak 1.0000 — the
master softclip ceiling engaging as designed (`≤ 0 dBFS`, the tanh asymptote
rounds to exactly 1.0 in float, per the Phase-2 amendment already documented in
`src/BlockwaveEngine.h`).

---

## 5. Rapid preset switching while notes are held

`[rapid_preset_switch_held_notes]` — a 4-note chord held, all 128 factory presets
loaded one after another, a new one every 3 blocks (8 ms at 48 k/128):

| Metric | Result |
|---|---|
| non-finite samples | 0 |
| peak | 0.9995 (≤ 0 dBFS) |
| mean \|x\| | 0.1252 |
| **audio-thread allocations** | **0** (guarded around every `processBlock`) |
| still sounding after the run | yes, tail peak 0.1967 |
| worst single-sample step | 0.4201 |
| 44.1 k / block 16, 128 switches | 0 non-finite, peak 1.0000 |
| 192 k / block 4096, 128 switches | 0 non-finite, peak 0.9906 |

`[preset_switch_click_matrix]` — 8 cross-category switches mid-note, scored at 4
swap instants each:

| From → to | step | strict ratio | widened ratio |
|---|---|---|---|
| PERMAFROST → MAGMA FLOOR | 0.0470 | 1.69 | 0.44 |
| MAGMA FLOOR → PERMAFROST | 0.0170 | 0.47 | 0.16 |
| SHARDSTORM → QUARRY KICK | 0.0807 | 0.72 | 0.47 |
| QUARRY KICK → SHARDSTORM | 0.0113 | 0.10 | 0.07 |
| COIN CHUTE → STATIC FIELD | 0.0001 | 1.22 | 0.00 (destination silences the note) |
| STATIC FIELD → COIN CHUTE | 0.0296 | 3.18 | 0.12 (destination silences the note) |
| FOREST LULLABY → KALIMBA COVE | 0.0120 | 0.60 | 0.16 |
| KALIMBA COVE → FOREST LULLABY | 0.0002 | 0.93 | 0.00 (destination silences the note) |
| negative control (hard splice, 5 pairs with a sounding source) | — | 1.74 … 9.48 | — |

**Finding F3 (closed, metric artifact).** STATIC FIELD → COIN CHUTE initially
scored 3.18 against the 2.5 gate. Per-parameter bisection (each of the 30
differing parameters swapped alone, and swapped-all-but-one) shows **no single
parameter clicks** — the largest single-parameter ratio is 1.11 — and that
removing `env1_s` (1 → 0) from the swap drops the full ratio from 3.18 to 0.26.
The destination preset has zero sustain, so the held note decays; the strict
bound is then measured on a decayed tail and collapses to 0.0093 while the
post-swap step (0.0296, −30.6 dBFS) is *smaller* than the 0.2056 step COIN CHUTE
produces natively on its own note. The test now uses the strict bound by default
and the destination's own steady slew when neither steady window represents
either patch; the case is flagged in the output. Honest limitation, stated in
the test: with two patches of similar brightness a hard splice scores as low as
1.74, so this matrix is weaker evidence than the Phase-4 craft-transition matrix
(splice ratio 20×+ there, same APVTS → engine path).

---

## 6. Multiple instances (CLAUDE.md rule 7)

`[multiple_instances]` — 8 `BlockwaveAudioProcessor` instances in one process,
each with a different preset, sample rate and block size; rendered once alone
and once with all eight running concurrently on 8 threads:

| # | Preset | Rate / block | peak | max \|alone − concurrent\| |
|---|---|---|---|---|
| 0 | SHARDSTORM | 44 100 / 16 | 0.3368 | 0 |
| 1 | MAGMA FLOOR | 48 000 / 128 | 0.2979 | 0 |
| 2 | QUARRY KICK | 88 200 / 512 | 0.4003 | 0 |
| 3 | PERMAFROST | 96 000 / 64 | 0.1642 | 0 |
| 4 | FOREST LULLABY | 176 400 / 1024 | 0.3099 | 0 |
| 5 | COIN CHUTE | 192 000 / 4096 | 0.7267 | 0 |
| 6 | KALIMBA COVE | 44 100 / 256 | 0.3322 | 0 |
| 7 | STATIC FIELD | 48 000 / 2048 | 0.1748 | 0 |

Bit-identical in every case: no shared mutable state, no cross-talk.

---

## 7. Extreme parameter states

`[extreme_parameter_rails]` — every one of the 67 parameters driven to its min
and its max with the other 66 at SPEC defaults, 134 renders:

| Metric | Result |
|---|---|
| non-finite samples | 0 |
| peak, worst | 0.5854 |
| rails above 0.98 | 0 |
| silent rails | 3 — `oscA_on` min, `oscA_level` min, `master_gain` min (silent by definition) |
| render time | median 2.30 ms, worst 4.11 ms (`sub_level`) — 1.8× median, no denormal stall (gate 25× median) |

`[parameter_fuzz]` — 300 random full-parameter states, seed `0xB10C0000C0DE`,
floats sampled through the APVTS taper (what a host randomising normalised
values produces), two notes each, 0.6 s at 48 kHz:

| Metric | Result |
|---|---|
| non-finite samples | 0 |
| worst peak | 0.3126 (state 54) |
| states hitting the 0.98 ceiling | 0 |
| silent states | 94 (random `oscA_on`/`master_gain`/level combinations) |
| worst \|DC\| | 0.0356 (extreme pulse widths — expected) |
| render time | median 1.70 ms, worst 3.90 ms — no stall (gate 40× median) |

Every state is reproducible from the seed and the state index; no parameter
combination misbehaved.

`ScopedNoDenormals` verified present at `plugin/PluginProcessor.cpp:89`, first
statement of `processBlock`. `src/FxChain.h` additionally flushes denormals
internally (`flushDenorm`) so the headless renderer is safe without FTZ/DAZ.

---

## 8. CPU under stress

`[cpu_stress_all_rates]` — 16 voices × 8-way unison = 128 stacks
(`kMaxUnisonStacks`), every source on, whole FX block plus all six wet-path
filters engaged, Release build, Apple silicon, single core:

| Sample rate | block 128 | block 512 | per voice (block 512) |
|---|---|---|---|
| 44 100 | **3.5 %** | 3.4 % | 0.21 % |
| 48 000 | 3.7 % | **3.8 %** | 0.23 % |
| 88 200 | 7.0 % | 6.8 % | 0.43 % |
| 96 000 | 7.5 % | 7.5 % | 0.47 % |
| 176 400 | 13.5 % | 13.5 % | 0.84 % |
| 192 000 | 14.6 % | 14.6 % | 0.91 % |

Cost scales linearly with sample rate (192 k ≈ 4.2× 44.1 k, matching the 4.35×
rate ratio) — no rate-dependent blow-up. The pre-existing FX budget gate (10.5 %
at 44.1 k/128 and 48 k/512) still holds with the same margin as the Phase-6
checkpoint: 3.5 % / 3.8 %, unchanged. New gate for the high rates: 25 %.

Regression vs previous checkpoint: **0 %** (44.1 k/128 was 3.5 %, 48 k/512 was
3.8 % at the Phase-6 checkpoint too). Flag threshold was 10 %.

---

## Findings

### F1 — abrupt tempo *increases* produce a bounded pitch-up glide in the delay (OPEN)

* Severity: cosmetic-audio, decision needed.
* Repro: `[tempo_jump_click_free]`, case 60 → 200 BPM. Wet-only 1/8 delay,
  `filt_cutoff` 900 Hz, sustained A3, tempo jumps at t = 1.5 s. Post-jump step
  0.0643 vs steady bound 0.0230 → ratio 2.80. Hard splice control: 20.41.
* Cause: delay-time smoothing moves the read pointer 0.5 s → 0.15 s in ~25 ms.
* Options: accept (documented behaviour), or crossfade between taps on tempo
  change. **Q1 to the architect.**

### F2 — plugin and `tools/render` resolve presets with one extra float round trip (CLOSED, informational)

* Severity: informational. Deviation −54.1 dBFS, worst case in the bank probed.
* Repro: `[offline_vs_plugin_path]`, preset STATIC FIELD. `filt_cutoff` 900 Hz
  (renderer) vs 899.999695 Hz (plugin).
* No action recommended; test bounds it at −40 dBFS so a real divergence would
  still fail.

### F3 — preset-switch click metric collapses when the destination silences the held note (CLOSED, test methodology)

* Severity: none for the product — no single parameter clicks (worst
  single-parameter ratio 1.11 out of 30 differing parameters).
* Repro and bisection in the test file comments; metric fixed and documented.

### F4 — CAVE reverb wet level is sample-rate dependent (OPEN)

* Severity: low but real. **−1.52 dB at 192 kHz vs 44.1 kHz.**
* Isolation (all measured, dry blip A3, 44.1 k vs 192 k RMS):

  | Path | deviation |
  |---|---|
  | dry | +0.02 dB |
  | delay wet only | +0.02 dB |
  | crush wet only | +0.06 dB |
  | **CAVE wet only** | **+1.54 dB** |

* Full curve (wet only, `cave_damp` 0, sustained A3):
  44.1 k 0.00 dB · 48 k −0.02 · 88.2 k −0.32 · 96 k −0.39 · 176.4 k −1.31 ·
  192 k −1.52.
* It is a **level** issue, not a decay issue: tail decay slope is −21.5 / −21.4 /
  −21.3 dB/s at 44.1 / 96 / 192 kHz, and the entire tail is offset downward.
  Independent of `cave_damp` (0.0, 0.8 and 1.0 all give the same ratio), smaller
  at `cave_size` 1.0 (1.16× vs 1.31×). RT60 gains and the damping coefficient in
  `src/FxChain.h` are correctly rate-compensated, so the suspect is the FDN input
  injection / line-length quantisation, not the decay math.
* User-visible effect: bouncing a project at 96 k makes reverb-dominated presets
  ~0.4 dB drier than the 44.1 k session; CAVE MOUTH (100 % reverb) shifts 3.4 dB
  peak / 2.5 dB RMS across the full rate range.
* Minimal repro:

  ```bash
  echo '{"formatVersion":1,"name":"PROBE","category":"FX","params":
        {"filt_cutoff":20000,"env1_s":1.0,"cave_mix":1.0,"cave_damp":0.0}}' > /tmp/probe.json
  build/release/render_artefacts/Release/render /tmp/probe.json note:A3:2.0s /tmp/a.wav --sr 44100
  build/release/render_artefacts/Release/render /tmp/probe.json note:A3:2.0s /tmp/b.wav --sr 192000
  # rms 0.160036 vs 0.134369 = 1.52 dB
  ```
* Guarded by `[cave_wet_level_rate_dependence]` at a 2.0 dB ceiling so it cannot
  worsen unnoticed. **Q2 to the dsp-engineer:** fix the injection gain, then
  tighten that ceiling to 0.2 dB.

---

## Notes for the checkpoint author

* No production code was changed by this work; `pluginval` was therefore not
  re-run (it was green at `54e3011`).
* The working tree also carries unrelated in-flight changes from another agent
  (`.github/workflows/ci.yml`, `.gitignore`, `CMakeLists.txt`, `README.md`,
  `docs/MANUAL.md`, `docs/RELEASE.md`, `scripts/package-macos.sh`). Nothing was
  committed.
* Suite runtime grew from 3.1 s to 29 s; the CPU sweep (12 × 2 s renders of the
  128-stack worst case) is ~19 s of that. If CI time matters, gate
  `robust::test_cpu_stress_all_rates()` to two rates and keep the full matrix
  for checkpoints.
