# RESONANCE / HARSHNESS QC — factory bank audit

Tool: `tools/qc/resonance.py` (stdlib only, same WAV IO and render-invocation
patterns as `tools/qc/renderpack.py`).
Raw per-preset numbers: `CHECKPOINTS/artifacts/RESONANCE_QC.csv` (572 rows =
128 presets × 3–5 pitches).
Scope: measurement and diagnosis only — **no preset was edited.**
Measured against the `render` binary built from HEAD `e7a16ac`. Another agent
was changing `src/FxChain.h` / `src/ParamSpec.h` concurrently; if the CRUSH or
filter DSP changed in that work, §4 M1 must be re-measured before the fixes are
applied (`python3 tools/qc/resonance.py --probe LICHEN_HARMONIUM`).

**Headline: the prime suspect is falsified.** Filter resonance is not the
cause of the harshness. The highest `filt_res` anywhere in the 128-preset bank
is **0.70** on one dark bass (TARPIT, cutoff 240 Hz); the median is 0.10. Every
preset was re-rendered with `filt_res` forced to 0.12 and the 2–6 kHz energy
share moved by at most **1.99 dB** across all 572 renders (median 0.00 dB).
There is no screech to remove because there is no resonance to remove.
What is actually making presets painful is **spectral sharpness**: thin pulse
widths played through a wide-open filter, plus **bitcrusher alias images that
sit after the filter and are therefore unfiltered**.

12 of 128 presets are flagged. 116 are clean.

---

## 1. Method

### 1.1 Rendering

Every preset goes through the real recipe-aware path — craft grid → recipe
override → params, i.e. exactly what the plugin runs:

```
build/release/render_artefacts/Release/render <preset.json> note:<N>:2s <out.wav> --sr 48000
```

**Across the register**, because the suspected mechanism only bites high up:

| Category | Pitches |
|---|---|
| LEAD, PLUCK, PAD, KEYS, CHIP | C2 C3 C4 C5 C6 |
| BASS | C1 C2 C3 C4 |
| FX | C2 C3 C4 |
| PERC | C2 C3 C4 (stays in its zone per SOUND_DESIGN) |

### 1.2 Sustain window (never the attack)

A 10 ms RMS envelope is built; the window starts **100 ms after the envelope
peak** (peak searched only in the first 1.5 s, so a slow CLOUD/PAD attack cannot
push it into the release) and ends where the envelope has fallen 30 dB, capped
at note-off. Short sounds fall back to a ≥150 ms window 30 ms after the
transient. This measures the ringing, not the click. Validated on the worst
percussive case: FLINT HAT reads 4.53 acum measured from the peak versus 4.39
from the automatic window — same verdict either way.

Spectrum: Welch, Hann, N = 8192 (5.86 Hz bins — fine enough to resolve C2's
65 Hz harmonic spacing), 50 % overlap, up to 8 frames, on the mono mid.

### 1.3 The four numbers

**SHARP (acum) — the primary detector.** Psychoacoustic sharpness
(Zwicker/DIN 45692, simplified): the centroid of specific loudness weighted by
`g(z)`, which climbs steeply above 15.8 Bark (~3.1 kHz). This is literally the
standard descriptor for "остро". Excitation = band energy through Terhardt's
outer/middle-ear transfer; specific loudness = E^0.23 (Stevens) instead of a
full loudness model; no upward spread of masking. Being a ratio it is
level-independent, so it separates *piercing* from merely *loud*.

**PROM (dB) — spectral peakiness.** How far the strongest narrow band
protrudes above its local neighbourhood. Measured on a **harmonic upper
envelope**, not on raw bins:

```
E(f)    = max power over bins within ±1.05·f0 of f      (window ≥ 2·f0, so it
                                                         always contains at
                                                         least one odd harmonic)
prom(f) = E_dB(f) − max( median E_dB over [f/2.0, f/1.4],
                         median E_dB over [f·1.4, f·2.0] )
```

Two design points that are not optional on this synth:

* *Envelope, not bins.* Every voice is a square/pulse — the spectrum **is** a
  comb of narrow lines. A bin-domain "peak minus smoothed spectrum" reports
  15–25 dB on a perfectly innocent square. Worse, a 50 % square has no even
  harmonics at all (>40 dB down), so "harmonic 3 vs harmonic 2" is +40 dB on a
  clean sound. The ±1.05·f0 max-window is what removes both traps, and it is
  only possible because we choose the note and therefore know f0 exactly.
* *Max of the two side medians (topographic prominence).* This makes a filter
  **knee** score ~0 dB: a non-resonant LP24 at 4 kHz is flat below and
  −24 dB/oct above, so the low-side median is already at the peak's level. Only
  a hump that protrudes above *both* neighbourhoods scores. Each side level is
  a median of 3 block medians, which survives the periodic sinc nulls of narrow
  pulse widths.

Candidate gate: `f ≥ 3·f0` (a hump under the 3rd harmonic is tone colour, not a
separate piercing band), `1.2 kHz ≤ f ≤ 9 kHz`, and the hump must be within
30 dB of the loudest partial. The envelope is clamped 80 dB below its maximum —
without that guard a neighbourhood driven to float32 zero produced unbounded
readings (60+ dB on a plain 50 % square).

**HF8K (%)** — sustain energy above 8 kHz / total above 20 Hz. Catches RAW
aliasing hash and crush grit, which are broadband and invisible to PROM.

**PAIN (%)** — sustain energy in 2–6 kHz / total. Reported for context; see
§5 for why it is *not* used as a threshold.

### 1.4 Causal ablation, not just flagging

Each preset is re-rendered with one parameter neutralised, merged into
`params` (applied after the craft grid — i.e. exactly the edit a fix would
make, so an ablation both attributes the cause and predicts the fix). Ablations
used: `filt_res`→0.12, `filt_keytrack`→0.6, `raw`→0, `crush_mix`→0,
`oscB_sync`→0, `lfo2_amt`→0, `uni_count`→1, `oscA_pw`→50, `filt_cutoff`→7000.
The `filt_res` ablation runs over the whole bank in `--scan`
(`RESLIFT_db` column); the rest are `--probe` / targeted.

### 1.5 Peak-motion test (what makes a peak "resonant")

For each preset the PROM peak frequency is compared between pitches an octave
apart. A peak pinned to a **fixed Hz** (filter resonance, crush alias image,
noise-band ring) is a whistle sitting on top of the note — the genuinely
"resonant" defect. A peak whose frequency **scales with the note** is source
structure: the pulse-width comb lobe or a sync formant, i.e. timbre.
Verified by ablation: forcing `oscA_pw` to 50 % collapses the tracking peaks
(RESONANT LODE 15.9 → 5.7 dB, SHARDSTORM 10.6 → 2.5 dB, CLOUD PIPER 6.1 →
−0.6 dB) and leaves the fixed ones untouched.

---

## 2. Thresholds and why

Anchored on a synthetic sweep (`--calibrate`: `filt_res` 0.10→1.00 × pulse
widths 12/25/50 % × unison × noise × LP12/BP × cutoff placement × C2…C6) and on
the observed bank distribution.

**Bank distribution (572 renders):**

| | p50 | p75 | p90 | p95 | p99 | max |
|---|---|---|---|---|---|---|
| SHARP (acum) at C4 | 1.50 | 1.98 | 2.19 | 2.30 | — | 4.39 |
| SHARP excess over the same-note bank median | 0.00 | 0.39 | 0.61 | 0.70 | 0.92 | 3.19 |
| PROM (dB) | −3.6 | 0.0 | 2.0 | 3.8 | 10.8 | 16.0 |
| HF8K (%) | 0.00 | 0.11 | 0.57 | 1.32 | 3.42 | 92.79 |
| RESLIFT (dB, resonance ablation) | 0.00 | 0.00 | 0.29 | 0.55 | 1.35 | 1.99 |

Per-note bank median SHARP (the register reference): C1 0.70, C2 1.20,
C3 1.35, C4 1.50, C5 2.10, C6 2.49.

**Rules:**

| Flag | Rule | Rationale |
|---|---|---|
| `SHARP` | SHARP ≥ **2.30 acum** *and* SHARP − same-note bank median ≥ **0.70** | 2.30 is the bank's C4 p95; 0.70 is the p95 of the excess distribution. Both gates are needed: sharpness climbs with register for *every* patch, so an absolute threshold alone would flag whole categories at C6; an excess threshold alone flags dark basses at C1 that are not sharp in absolute terms (MAGMA FLOOR, excess +1.15 at 1.85 acum, correctly excluded). |
| `FIXPEAK` | PROM ≥ **6.0 dB**, within 30 dB of the loudest partial, *and* the peak does not track the keyboard | 6.0 dB sits between the bank p95 (3.8) and p99 (10.8) — the knee of the tail. `mixed` motion counts as source structure and is not flagged; `n/a` (peak present at one pitch but not its octave) is flagged, since a peak that vanishes an octave away cannot be following the keyboard. |
| `HASH` | HF8K ≥ **20 %** | The tail is p99 = 3.4 %, then 6.5 %, 8.0 %, then 92.8 %. Nothing sits between 8 and 92, so 20 % is an uncontroversial cut and catches exactly the one hash-dominated preset. |

Calibration sanity check: on the synthetic sweep, `filt_res` 0.10 → 1.00 at
cutoff 3 kHz raises PAIN from 1.09 % to 11.94 % at C4 and from 22.4 % to 81.1 %
at C6, and PROM rises monotonically with resonance at every pulse width — the
metric does respond to real resonance. It simply never fires on this bank,
because this bank has none.

---

## 3. Flagged presets (12 / 128)

| Preset | Cat | Worst pitch | SHARP | vs bank | PROM | HF8K % | PAIN % | Flags |
|---|---|---|---|---|---|---|---|---|
| FLINT HAT | PERC | C2 | 4.39 | +3.19 | 12.9 dB @ 8620 Hz | 92.79 | 2.0 | SHARP+FIXPEAK+HASH |
| FLINT WHISTLE | LEAD | C6 | 3.30 | +0.82 | 0.0 @ 3183 | 8.02 | 76.7 | SHARP |
| MIDAS MODE | CHIP | C6 | 3.21 | +0.72 | 0.7 @ 7249 | 1.80 | 40.8 | SHARP |
| CANYON CRIER | LEAD | C6 | 3.20 | +0.72 | −1.7 @ 3624 | 3.04 | 62.3 | SHARP |
| BLIP BROOK | CHIP | C6 | 3.20 | +0.71 | −0.6 @ 3183 | 2.77 | 61.4 | SHARP |
| BONUS STAGE | CHIP | C6 | 3.19 | +0.71 | −2.7 @ 3624 | 2.76 | 61.4 | SHARP |
| LICHEN HARMONIUM | KEYS | C6 | 2.94 | +0.45 | **9.1 @ 5126** | 0.29 | 0.5 | FIXPEAK |
| SMOKE SIGNAL | PAD | C6 | 2.61 | +0.13 | **6.4 @ 7249** | 0.24 | 4.8 | FIXPEAK |
| FANFARE FALLS | CHIP | C4 | 2.42 | +0.92 | −3.6 @ 4501 | 0.79 | 5.4 | SHARP |
| HALF GATE | CHIP | C4 | 2.39 | +0.89 | −3.0 @ 2715 | 0.45 | 3.2 | SHARP |
| JINGLE SPRING | CHIP | C4 | 2.37 | +0.86 | 3.2 @ 1812 | 0.66 | 7.2 | SHARP |
| SUNSPIKE | LEAD | C4 | 2.36 | +0.86 | 5.9 @ 1210 | 0.05 | 14.2 | SHARP |

---

## 4. Diagnosis by mechanism, and the fix for each

Every "after" figure below was produced by actually rendering the proposed
override and re-measuring — these are measurements, not predictions.

### M1 — Crusher alias images landing in the pain band (2 flagged + 2 near-miss)

**The clearest genuine "resonant whistle" in the bank, and the least obvious.**
CRUSH sits *after* the filter in the FX chain, so its sample-rate-reduction
images are never filtered. The images land at `48000 / crush_down` Hz and are
**pinned to a fixed frequency** — they do not move with the keyboard, which is
exactly what a producer hears as "резонансно" sitting on top of an otherwise
mellow patch.

Confirmed by arithmetic and by ablation:

| Preset | crush_down | predicted image | measured PROM peak | crush_mix→0 |
|---|---|---|---|---|
| LICHEN HARMONIUM | 9 | 48000/9 = 5333 Hz | **5126 Hz**, +9.1 dB | SHARP 2.94 → **1.42**, peak gone |
| TAPE SUNRISE | 13 | 48000/13 = 3692 Hz | **3677 Hz**, +3.4 dB | SHARP 2.09 → **1.30** |
| SMOKE SIGNAL | 6 | 48000/6 = 8000 Hz | 7249 Hz, +6.4 dB | SHARP 2.61 → **1.89** |
| VHS CLIFF | 13 | 3692 Hz | 2250 Hz, +2.1 dB | SHARP 2.05 → **1.69** |

LICHEN HARMONIUM is the standout: a KEYS patch with `filt_cutoff` **1755 Hz** —
deliberately dark — carrying a +9 dB metallic tone at 5.1 kHz that the filter
cannot touch. Recipe is `KEYS + MOSS + CLOUD`, and MOSS is what brings
`crush_down +8×`.

**Fix — lower `crush_down` so the image clears the pain band** (image frequency
goes *up* as crush_down goes *down*; `crush_down ≤ 5` puts it above 9.6 kHz),
and/or trim `crush_mix`:

| Preset | Recommended override | Verified |
|---|---|---|
| LICHEN HARMONIUM | `crush_down 9 → 4`, `crush_mix 0.25 → 0.15` | SHARP 2.94 → 2.45 @C6; 2.06 → 1.55 @C4; 5126 Hz peak eliminated |
| SMOKE SIGNAL | `crush_down 6 → 3`, `crush_mix 0.25 → 0.15`, `master_gain 3.6 → 1.5` | SHARP 2.61 → 2.13 @C6; peak 6.4 dB → −5.7 dB |
| TAPE SUNRISE | `crush_down 13 → 4` (keep `crush_mix 0.38`) | SHARP 2.09 → 1.64 @C4; peak 3.4 dB → −6.6 dB |
| VHS CLIFF | `crush_down 13 → 4` | SHARP 2.05 → 1.89 @C4 |

Caveat for the designer: `crush_down` *is* the lo-fi character. If the MOSS
grit must stay, cut `crush_mix` instead — the audible penalty is the same
direction but smaller. Note `crush_hp` (SPEC addendum) will **not** help: it
high-passes the crusher input, it does not remove the images.

### M2 — Thin pulse + wide-open filter, no keytracking (9 flagged)

The bulk of the flags. `oscA_pw` 14–25 % puts most of the energy into high
harmonics; `filt_cutoff` is then left at 12–20 kHz, so nothing is removed. At
C4 these read 2.3–2.5 acum; at C6, 3.0–3.3. This is broadband, not a peak —
PROM is near zero for most of them, which is why the sharpness metric rather
than the peakiness metric is what catches them.

**Important correction to the working hypothesis: adding keytracking makes
these worse, measurably.** `filt_keytrack → 0.6` on FLINT WHISTLE raises SHARP
at C6 from 3.30 to **3.61**; on GEODE VOICE at C6 from 2.79 to **3.18**; on
SUNSPIKE at C5 from 2.72 to **3.07**. With the cutoff already above the top of
the harmonic series, keytracking only opens it further on high notes. Do not
apply keytrack as a blanket fix here.

**Fix — bring the cutoff back to 6–9 kHz.** Verified:

| Preset | Current | Recommended | Verified |
|---|---|---|---|
| FLINT WHISTLE | cut 12600, pw 14, oct +1, res 0.10, kt 0.00 | `filt_cutoff 12600 → 7000` | SHARP 3.30 → 2.83 @C6, 2.46 → 2.08 @C4 |
| BLIP BROOK | cut 20000, pw 14, gain +6.0 | `filt_cutoff 20000 → 9000` (and see M4 re gain) | 3.20 → 2.64 @C6 |
| BONUS STAGE | cut 20000, pw 14, gain +5.0 | `filt_cutoff 20000 → 9000` | 3.19 → 2.63 @C6 |
| JINGLE SPRING | cut 20000, pw 25 | `filt_cutoff 20000 → 9000` | 3.04 → 2.44 @C6, 2.37 → 2.00 @C4 |
| CANYON CRIER | cut 15750, pw 14, kt 0.30, gain +3.2 | `filt_cutoff 15750 → 8000` | 3.20 → 2.80 @C6 |
| MIDAS MODE | cut 15000, pw 25, uni 8, kt 0.40 | `filt_cutoff 15000 → 9000` | 3.21 → 3.03 @C6, 2.27 → 2.00 @C4 |
| FANFARE FALLS | cut 17000, pw 50, raw ON | `filt_cutoff 17000 → 9000` | 2.42 → 2.06 @C4, 2.77 → 2.31 @C6 |
| HALF GATE | cut 17000, pw 50, raw ON | `filt_cutoff 17000 → 9000` | 2.39 → 2.03 @C4, 2.85 → 2.36 @C6 |
| SUNSPIKE | cut 9000, pw 18, oct +1, uni 5 | `filt_cutoff 9000 → 6000` | 2.36 → 2.08 @C4, 2.72 → 2.37 @C5 |

Near-misses in the same class, worth the same treatment if the producer agrees
with the direction: SCANLINE SOLO (2.87 @C6, `filt_cutoff 20000 → 9000` gives
2.37 and drops HF8K 6.50 % → 0.30 %), NARROW GATE (3.14), COIN CHUTE (3.18),
MIRROR LAKE (3.05), COBALT CRY (2.88).

### M3 — All-treble noise percussion (1 flagged)

**FLINT HAT** is the single most extreme preset in the bank by a wide margin:
SHARP **4.39 acum** against a bank C2 median of 1.20 — an excess of +3.19 where
the next-worst is +1.15. HF8K = **92.8 %**: the sustain is essentially nothing
but top-octave noise. Recipe `PERC + SAND + STONE` with overrides
`oscA_on:false, noise_mode:short, filt_type:HP, filt_cutoff:6500,
master_gain:+6.0`. So: LFSR-short noise, high-passed at 6.5 kHz, with the
highest master gain in the bank. RAW ablation changes nothing (SHARP 4.39 both
ways), so this is not aliasing — it is the intended signal chain, just far too
bright and far too loud.

**Fix:** `filt_cutoff 6500 → 3800`, `master_gain 6.0 → 1.5` — verified SHARP
4.39 → **3.93**. Note this only takes it to the top of the flag range, not out
of it; a hat *is* a bright sound. If the producer wants it fully tamed, add
`noise_level 0.85 → 0.65` and consider LP24 with a 12 kHz cutoff in series
rather than a bare HP. Recommend the producer A/Bs this one by ear before the
final value is chosen — it is a taste call, not a defect call.

### M4 — Not a defect: pulse-width comb lobes (documented so nobody "fixes" them)

RESONANT LODE (PROM **16.0 dB** @ 4501 Hz, the highest peakiness in the bank),
SHARDSTORM (11.7 dB), GEODE VOICE (8.1 dB), CLOUD PIPER (6.1 dB) and
MIDAS MODE (9.7 dB @ 1812 Hz) all carry large narrow peaks. **None of them are
resonance.** Evidence:

* `filt_res → 0.12` changes them by ≤ 0.3 dB.
* `oscB_sync → 0`, `lfo2_amt → 0`, `crush_mix → 0`, `raw → 0` all change them by
  ≤ 1 dB (except GEODE VOICE, whose peak halves when `lfo2_amt→0` — its LFO2
  drives PW).
* `oscA_pw → 50 %` collapses them: RESONANT LODE 15.9 → 5.7, SHARDSTORM
  10.6 → 2.5, CLOUD PIPER 6.1 → −0.6, MIDAS MODE 9.7 → 6.1 (and → 3.7 with
  `uni_count → 1`).
* Their peak frequency scales exactly with the note (RESONANT LODE: 1281 Hz at
  C3 → 2250 at C4 → 4501 at C5).

They are the sinc lobes of an 18 %-duty pulse — the intended nasal bite of the
"thin pulse lead" technique. Leave them alone. This is also the main **false
positive** class of the PROM metric and the reason the peak-motion test exists.

### M5 — Secondary observation: loudness, not timbre

Five of the flagged presets carry positive `master_gain` (FLINT HAT +6.0,
BLIP BROOK +6.0, BONUS STAGE +5.0, SMOKE SIGNAL +3.6, CANYON CRIER +3.2), as
does GLINT CELESTA (+6.0, sharpness 2.69 at C6). Sharp *and* hot is what makes
a preset stab when the producer pages through the browser. The bank was
loudness-matched on RMS, which under-weights the 2–6 kHz region the ear is most
sensitive to, so a bright preset matched by RMS is perceptually louder than a
dark one. **Proposal (out of scope for this pass, not applied):** re-run the
final bank loudness match on a K-weighted level rather than plain RMS. The
`K_db` column in the CSV is the per-render K-weighted level and is a starting
point, though it is measured on the sustain window only and so under-reads
short percussive sounds.

---

## 5. Confidence, and what this metric can miss

**High confidence:**
* Filter resonance is *not* the cause. This is close to certain — it is a
  direct parameter reading (max `filt_res` = 0.70) confirmed by a whole-bank
  ablation.
* The crusher alias mechanism (M1). The predicted image frequency
  `48000/crush_down` matches the measured peak to within 4 % on the two cleanest
  cases, and removing the crush removes the peak. This is a genuine defect and
  the most likely thing the producer actually heard on a "mellow" patch.
* FLINT HAT being an outlier. It is 2 acum clear of the entire bank.

**Medium confidence:**
* The ranking within the M2 group. Sharpness correlates well with perceived
  piercing, but 2.36 vs 2.42 acum is not a meaningful difference — treat the
  M2 list as a set, not an ordering.
* The specific cutoff values recommended. They were chosen to bring SHARP
  back to roughly the bank median for the category and verified to do so, but
  the musical cost (a chip lead losing its bite) is a taste call for the
  producer. Every one is a single-parameter change and trivially reversible.

**Known limits — what this will miss:**
1. **No listening was possible.** Every claim here is measurement plus
   mechanism. The sharpness metric has never been validated against Kirill's
   ears on this bank. If his complaints do not intersect this list of 12, the
   thresholds are wrong, not the presets.
2. **Single held notes only.** Harshness from chords (intermodulation between
   detuned unison voices), from fast repeated notes, or from the delay/cave
   tails building up over a phrase is not measured. `renderpack.py` renders
   category phrases; merging that into this tool is the obvious next step.
3. **Attack transients are deliberately excluded.** A preset with a vicious
   `env2 → cutoff` click but a mellow sustain reads clean here. Filter-envelope
   screech is precisely the SOUND_DESIGN anti-pattern ("resonance > 0.8 with
   high env amounts") and this pass cannot see it — though with max res = 0.70
   it is unlikely to exist.
4. **PROM is a weak discriminator by construction.** On the synthetic sweep,
   sweeping `filt_res` 0.10 → 1.00 (a +22 dB filter peak) moves PROM by only
   ~4 dB, because a lowpass resonance sits on the shoulder of its own skirt and
   on the source's −6 dB/oct tilt, both of which raise the comparison
   neighbourhood. PROM reliably detects *narrow fixed* peaks (crush images,
   noise-band rings); it should not be trusted as a resonance meter in
   isolation. This is why the flag rule leads with sharpness.
5. **Pulse-width comb lobes are the main false-positive source** for PROM,
   mitigated but not eliminated by the peak-motion test (see M4).
6. **Sharpness is a simplified Zwicker model.** Absolute acum values are not
   calibrated against a reference; only the ranking and the relative excess are
   meaningful.
7. **PAIN (2–6 kHz share) is reported but not thresholded**, because above C5 it
   mostly measures "the fundamental is now in the 2–6 kHz band" rather than any
   defect — THORN FLICK reads 99.98 % at C6 simply because it is an oct+1 pluck
   played at C6. Do not flag on it.
8. **Sample-rate dependence.** The crush image frequency is `SR/crush_down`, so
   at 44.1 kHz M1's images move to 4900 Hz (LICHEN HARMONIUM) — still in the
   pain band. The recommended fixes hold at both rates, but the numbers here
   are 48 kHz only.

---

## 6. Reproducing

```
python3 tools/qc/resonance.py --calibrate                # synthetic ground truth
python3 tools/qc/resonance.py --scan                     # whole bank -> CSV + summary
python3 tools/qc/resonance.py --probe LICHEN_HARMONIUM   # detail + ablation A/B
```

`--scan` takes ~90 s for 128 presets × 4 pitches × 2 renders (shipped +
resonance ablation) and writes `CHECKPOINTS/artifacts/RESONANCE_QC.csv`.
