# SOUND DESIGN BIBLE — techniques & the 128-preset factory

Reference for `preset-designer`. Everything here must be expressible through the SPEC parameter set.

## Why squares are enough

A square wave contains only odd harmonics (1, 3, 5...) at 1/n amplitude — hollow, woody, "chippy". A **pulse** (PW ≠ 50%) reintroduces even harmonics; sweeping PW (PWM) creates the illusion of two detuned oscillators — the classic lush movement. Between PW extremes, detune stacks, hard sync, LFSR noise and a filter, the square family covers leads, basses, pads, keys, percussion and FX. Constraint is the brand.

## Technique catalogue (use these, cite them in preset descriptions)

1. **PWM pad:** PW 50%, lfo1_pwm 0.3–0.6 at slow synced rate, uni_count 4–6, detune 10–20c, slow attack, CAVE. The money pad sound.
2. **Thin pulse lead:** PW 10–18% = nasal/oboe bite; add uni 2–3 and slight delay.
3. **Hollow 50% keys:** clarinet-like; WOOD-style low cutoff, medium release, vel_amp high for playability.
4. **Supersquare:** uni_count 7–8, detune 25–45c, spread 1.0 — the square answer to a supersaw. Anthem leads, gated pads.
5. **Sync scream:** oscB_sync on, sweep oscB_semi +3..+19 via env2 or lfo2 — aggressive formant leads.
6. **Sub-anchored bass:** OSC A one octave up with character (PW 30–40%, slight crush), SUB −1 carrying the weight, cutoff 200–800 Hz, short-ish release.
7. **Pitch-env pluck:** env2_pitch +7..+24 st, env2 decay 30–80 ms, sustain 0 — mallet/karplus-ish attack on anything.
8. **Chip drums:** kick = env2_pitch −24..−36 st, decay 60–120 ms, sine-ish via cutoff way down; snare = short noise burst (LFSR long) + tiny pitch drop; hat = LFSR **short** mode, 15–40 ms, HP filter. All authentically square-family.
9. **NES metallic:** noise_mode short at different filter tunings = the iconic robotic timbre; great texture layer.
10. **Chip arps (faked):** no arp engine in v1 — use lfo2 square→pitch at ±12 st, fast synced rate, for octave-jump chiptune energy; label these CHIP category.
11. **Lo-fi haze:** crush_down 6–16×, crush_mix 0.2–0.4, MOSS-style slow pitch wobble ±4–8c — tape/cassette vibe.
12. **RAW dirt:** raw ON for aliasing shimmer in high leads — intentional, tag such presets "RAW".
13. **Cave ambience:** CLOUD-style long attack + cave_size 0.7+ + dly_pingpong low mix — Minecraft-adjacent calm without copying anything.
14. **Glide talk:** mono/legato + glide 60–200 ms on thin pulses = expressive vocal leads; SLIME wobble variant for wubs.

Anti-patterns: resonance > 0.8 with high env amounts (screech), release > 3 s on basses (mud), crush_bits < 4 on full chords (noise floor), everything at max unison (CPU + wash).

## The 128-preset plan — "two full stacks of 64"

| Category | Count | Brief |
|---|---|---|
| LEAD | 24 | thin-pulse, supersquare, sync, RAW retro, glide vocal |
| BASS | 20 | sub-anchored, reese-ish detuned, wub (SLIME), 8-bit stabs |
| PLUCK | 16 | pitch-env, glassy, karplus-ish, dry STONE plucks |
| PAD | 16 | PWM lush, cave ambient, dark OBSIDIAN drones→pads |
| KEYS | 12 | hollow clarinet keys, bell-ish sync keys, lo-fi piano-adjacent |
| CHIP | 16 | faked arps, RAW leads, NES metallics, game-jingle tones |
| PERC | 12 | kicks, snares, hats, toms, zaps — LFSR + pitch env |
| FX | 12 | risers, drops, textures, drones, "cave noises"-style ambiences |

Naming: blocky/mining/nature vocabulary, original coinages, ALL CAPS, memorable: FROSTBYTE, COBBLE BASS, DIAMOND PLUCK, 8BIT SUNSET, DEEP SHAFT, PIXEL RAIN. Never Mojang-coined words.

## Factory pipeline (Phase 6)

1. Every preset is authored as **craft recipe + minimal overrides** (teachability rule). preset-designer writes ~**300 candidates** as JSON with a one-line design note each, spread across categories ×2 of target.
2. `tools/render` batch-renders each candidate: single C4 2 s + a short category-specific MIDI phrase (leads: melody; bass: groove; perc: pattern; pads: held chord) at 120 BPM, 48 kHz.
3. Auto-QC pass rejects: silent (< −50 LUFS), clipping, DC offset, near-duplicates (spectral centroid + MFCC distance below threshold), CPU hogs. Normalize survivors to ≈ −18 LUFS integrated for fair listening.
4. Human cull: Kirill listens to the survivor folder (files prefixed `CATEGORY_NN_NAME.wav`) and marks keepers; trim to exactly 128 with category quotas above.
5. Final pass: loudness-match factory bank, verify every preset's browser recipe icon renders, spell-check names, lock the bank.
