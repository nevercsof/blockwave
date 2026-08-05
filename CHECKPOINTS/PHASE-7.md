# CHECKPOINT — Phase 7 (QA, hardening, packaging)

Date: 2026-08-03 · Version **1.0.0-rc1** · **RUN STOPS HERE — Kirill runs the FL Studio checklist.**

## Summary

Everything automatable in Phase 7 is done and green. The suite went **3024 → 4709 checks**; the sweeps found four issues, of which the one real defect is fixed and the rest are documented below. Packaging, versioning and the public-facing documentation exist. What remains is the half a machine cannot do: loading in a real host, and ears.

## Automated robustness (report: `CHECKPOINTS/artifacts/PHASE7_ROBUSTNESS.md`)

| Sweep | Result |
|---|---|
| Sample rates 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz | ✅ **768 renders** (128 presets × 6 rates): 0 non-finite, max peak 0.9461, pitch error median 1.78 ¢ |
| Buffer sizes 16…4096 + pathological sequences (`1,4096,3,2048,7`, all-1-sample, mid-session re-prepare) | ✅ **bit-identical** everywhere, also at 44.1 k and 192 k |
| Offline vs plugin path | ✅ deviation **0** on identical parameters |
| Tempo: 14 locks + 7 abrupt jumps + a 33-step ladder incl. 33 and 999 BPM | ✅ echo spacing error **0 samples** every time |
| 128 presets switched under a held chord | ✅ 0 non-finite, **0 audio-thread allocations**, still sounding after |
| 8 concurrent instances, 6 rates × 8 block sizes | ✅ **bit-identical to running each alone** (CLAUDE.md rule 7) |
| 67 params × 2 rails + 300-state seeded fuzz | ✅ 0 non-finite, max peak 0.5854, no stall |
| CPU stress | ✅ 3.5 % @44.1k/128 · 3.8 % @48k/512 · 7.5 % @96k · 14.6 % @192k — budget holds, **0 % regression** |

Final gates on this tree: clean build **0 warnings**, ctest **4709 checks / 0 failures**, **pluginval s10 SUCCESS on VST3 and AU**.

## Defect found and fixed — F4, CAVE reverb wet level drifted with sample rate

**Symptom:** the reverb was 1.52 dB quieter at 192 kHz than at 44.1 kHz. Decay slope was correct at every rate (−21.4 dB/s), which pointed at injection rather than decay math.

**Root cause:** the DC blocker on the mono send *into* the FDN hard-coded its pole as `0.995f` instead of deriving it from the sample rate. A bare coefficient pins the corner in *normalised* frequency, so the real corner rides the rate: ~35 Hz at 44.1 k, ~76 Hz at 96 k, ~153 Hz at 192 k. At 192 kHz that high-pass sat under the A3 fundamental and ate the signal on its way in. The giveaway was that the error grew as the **square** of the rate (−0.02 / −0.32 / −0.39 / −1.31 / −1.52 dB) — the closed form of a one-pole HP loss, i.e. the signature of a fixed coefficient, not of anything length-related.

**Fix:** the pole is computed in `prepare()` from the sample rate — restoring the filter's intended corner, not trimming the output afterwards. The codebase already had the correct precedent in `LfsrNoise::prepare`. Spread **1.52 dB → 0.07 dB**, in line with dry (0.02), delay (0.02) and crush (0.06). The 44.1 kHz render is **byte-identical** before and after, proving the reference rate is untouched. The characterisation test qa-runner had left at a 2.0 dB ceiling was tightened to **0.2 dB**, which fails if the hard-coded pole ever comes back.

**Regenerated goldens: exactly one pair** — `tests/golden/fx_wet_default_48k.{f32,wav}`. That test renders at 48 kHz with `cave_mix 0.5` and is a strict bit-exact golden; the pole legitimately moves 0.995 → 0.99540625 there (max diff 0.00288, −42 dB below peak). The other three golden pairs are byte-identical and still verify at max |diff| = 0. **All 25 craft hashes untouched** — craft is parameter math and never reaches audio.

## Packaging, versioning, docs

- **Version 1.0.0-rc1.** CMake and JUCE both require a strictly numeric version (it becomes a binary version code), so the numeric `1.0.0` goes into the binaries and the `-rc1` tag is carried separately as `BLOCKWAVE_VERSION_STRING` for humans, zip names and the UI. Shipping final = clearing one string. A generated `blockwave-version.txt` is what both packaging paths read, so a zip can never be named for a version other than its contents.
- **macOS**: `scripts/package-macos.sh` → `dist/BLOCKWAVE-1.0.0-rc1-macOS.zip`, **9.5 MiB**, VST3 + AU + Standalone + LICENSE + INSTALL.txt, verified by round-tripping the zip. INSTALL.txt covers both plug-in folders, the exact `xattr -dr com.apple.quarantine` commands, per-host rescan steps, right-click→Open plus the macOS 15 "Open Anyway" fallback, and uninstall.
- **Windows**: two additive CI steps, gated on tag pushes and manual runs only — the per-push build/pluginval matrix is byte-identical to before. Verified offline by extracting the step's script and running it against a fake build tree.
- **README** rewritten for a public audience; **`docs/MANUAL.md`** covers all 67 controls in plain language; **`docs/RELEASE.md`** documents the release mechanics.
- **Recipe safety verified programmatically**: all 16 recipe names grepped against README, MANUAL, RELEASE and the packaging scripts — zero hits. The manual says only that 16 exist and are found by experiment.

## Open questions

1. **Q1 (taste — Kirill, item 25 on the checklist): abrupt tempo jumps make the synced delay glide** its read pointer to the new tap over ~25 ms — a tape-style pitch-zip. Bounded, not a discontinuity: 2.80 on the click ratio against a 2.50 gate, where a true hard splice scores 20.4. Accept as character, or crossfade the taps?
2. **Q2 (architect): `loadPresetVar` runs one float round-trip more than the render tool's path** (`filt_cutoff` 900 → 899.999695 Hz). Irrelevant on 127 presets, but on one chaotic patch (STATIC FIELD) it diverges to −54.1 dBFS. Bounded by a test at −40 dBFS. Unify the two paths, or accept?
3. **Q3 (architect): pre-existing VST3 seal.** JUCE ad-hoc signs the bundle and *then* writes `moduleinfo.json` into it, breaking the seal — `codesign -v` fails on unmodified builds at HEAD too. Hosts load it regardless. An opt-in `--adhoc-resign` flag exists in the packaging script. Ship as-is, or re-sign by default?
4. **Q4 (ui-engineer, one line): show `BLOCKWAVE_VERSION_STRING` in the top bar.** The define is already wired to every target that needs it; today only the numeric 1.0.0 is visible, via the host's plugin manager.
5. Suite runtime grew 3.1 s → 29 s, ~19 s of it the six-rate CPU matrix. Acceptable, but worth knowing before it grows again.

## Remaining Phase 7 DoD — human and external

| DoD item | Status |
|---|---|
| FL Studio checklist executed on Kirill's Mac | ⏳ **`CHECKPOINTS/FL_STUDIO_CHECKLIST.md`** — 31 numbered items + 5 taste questions, ~20 min |
| Windows: CI pluginval | ✅ green every push |
| Windows: small public beta round (3–5 testers) | ⏳ not started — needs Kirill to recruit; the Windows zip is ready to hand them |
| Installers/zips verified | macOS ✅ built and round-trip verified · Windows ⏳ needs one live CI run (Actions → CI → Run workflow) |
| Zero known crashes | ✅ none found across every sweep |
| Final pluginval logs archived, both platforms | ✅ macOS in `CHECKPOINTS/logs/`; Windows per-run in CI artifacts |

## Artifacts

- `CHECKPOINTS/FL_STUDIO_CHECKLIST.md` · `CHECKPOINTS/artifacts/PHASE7_ROBUSTNESS.md` · `CHECKPOINTS/artifacts/RESONANCE_QC.md`
- `dist/BLOCKWAVE-1.0.0-rc1-macOS.zip` (git-ignored) · `scripts/package-macos.sh`
- `docs/MANUAL.md` · `docs/RELEASE.md` · rewritten `README.md`
- pluginval logs: `CHECKPOINTS/logs/` · Repro: `bash scripts/build.sh` · `cd build/release && ctest` · `scripts/validate.sh CHECKPOINTS/logs`

## Next

Phase 8 (launch kit) after the checklist comes back and its findings are fixed. Kirill's own task there — the demo beat made entirely in BLOCKWAVE — is the one thing nobody else can do, and it is the best marketing asset we will have.
