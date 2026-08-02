# NEXT SESSION — where we stopped and how to restart fast

Written 2026-08-02. Read this first, then `CHECKPOINTS/PHASE-5-4.md`.

## State

Phases **0, 1, 2, 3, 5, 4** are done, verified by qa-runner, committed and pushed.
Repo: https://github.com/nevercsof/blockwave · last commit `d5cd113` · **CI green on macOS + Windows** (pluginval s10 SUCCESS on both).

- 580 tests green (Release and Debug), 157 build targets, zero warnings.
- CPU worst case 3.6 % of one core @ 44.1k/128 — huge headroom.
- The synth is playable end to end: craft a patch on the 3×3 grid, hunt one of 16 recipes, tweak all 61 params, FX live, presets save/load.

Nothing is running. No background agents, no uncommitted work.

## The only outstanding work (2 known defects, both diagnosed, neither blocks review)

1. **Grid-change click** — CRAFT_GRID.md §4 violation. 10 of 12 probed mid-note transitions glide correctly; 2 fail, both starting from `PAD + OBSIDIAN×8`: → `BASS bare` (step 0.105, ratio 4.32) and → `BASS+LAVA` (step 0.293, ratio 12.11, ≈ −11 dBFS — audible). Proven a genuine waveform discontinuity, not an envelope artifact. Reproduces at 44.1k/48k, buffers 64/512.
   - dsp-engineer reproduced it and stalled during engine-level bisection; the diagnosis was not finished.
   - Likely suspects to check first: an un-smoothed discrete parameter stepping (octave / semitone / oscillator-enable / sub-octave) or an instant phase-discontinuous change when the base archetype and oscillator configuration change at once. OBSIDIAN×8 sits on the octave clamp rail.
2. **Recipes 9–16 have no automated trigger test.** `tests/CraftTests.h:245` hardcodes `n == 8`; `tests/StateTests.cpp:639` asserts only `>= 8`. All 16 were proven working by a throwaway harness, but a table change could break the eight new ones with CI staying green. Promote to a real data-driven test (iterate the book, so future recipe updates need no test edits).

## To restart, paste this

> Прочитай CHECKPOINTS/NEXT-SESSION.md и CHECKPOINTS/PHASE-5-4.md. Продолжи с двух незакрытых дефектов: щелчок при смене грида (CRAFT_GRID §4) и отсутствие автотеста для рецептов 9–16. Делегируй dsp-engineer с эффортом high, затем qa-runner. Ответы архитектора на 9 открытых вопросов: <вставить сюда>.

If the architect has answered the open questions, paste the answers in — several of them (especially #1 and #6) change engine tables and golden hashes, so they should land in the same pass as the fixes.

## Build / verify commands

```
bash scripts/build.sh                      # Release: VST3 + AU + Standalone + render + tests + screenshots
cd build/release && ctest --output-on-failure
scripts/validate.sh CHECKPOINTS/logs       # pluginval s10, VST3 + AU
./build/release/blockwave_screenshots_artefacts/Release/blockwave_screenshots CHECKPOINTS/screenshots
./build/release/render_artefacts/Release/render --craft-matrix CHECKPOINTS/artifacts/craft_matrix --sr 48000
```

The craft matrix (120 WAVs, 66 MB) is `.gitignore`d and must be regenerated locally with the last command — it is deterministic, so it always comes back identical.

## After the fixes: Phase 6 (preset factory)

Phase 6 needs Kirill's ears — it is the phase with the human in the loop: ~300 candidates → auto-QC → he listens to a folder of WAVs → cull to 128. Recommended effort: **medium** for the pipeline, but budget real listening time.

Two Phase-4 findings will bite Phase 6 if unresolved: the short-LFSR DC offset (open question #2 — Phase 6 auto-QC rejects DC) and whether `master_gain` trims live in recipes or in loudness-matching (open question #7).
