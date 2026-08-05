# CRAFT GRID — Killer Feature Spec

The hook: **you don't tweak knobs, you craft sounds.** Place a BASE block in the center of a 3×3 grid, surround it with MATERIAL blocks, and the synth deterministically "crafts" a patch. Certain exact patterns are hidden **recipes** that unlock named signature sounds — the community hunts and shares them like crafting recipes. One reviewer discovering a recipe on camera is the whole marketing plan.

## Mechanics

1. **Center cell = BASE (required).** One of 8 archetypes, each a hand-tuned starting patch: `LEAD, BASS, PAD, PLUCK, KEYS, CHIP, PERC, DRONE`.
2. **8 outer cells = MATERIALS (optional, repeatable).** Each material applies a set of parameter deltas to the base. Stacking the same material intensifies it with diminishing returns: copy weights `1.0, 0.5, 0.25, 0.125...` (sum of weights scales that material's delta set).
3. **Deterministic:** `craft(base, cells[8]) -> full parameter set`. Same grid = same sound, always, on every machine. Position does NOT matter for normal crafting (shapeless), EXCEPT for recipe detection.
4. On any grid change: recompute params on the message thread, write atomically, audible params glide over ~30 ms (no clicks).
5. **Auto-naming:** generated from material adjectives + base noun ("Frozen Golden Lead", "Volatile Mossy Bass"). SAVE captures grid + name into a user preset.
6. **DICE** = fill random cells with random materials (base kept). **MUTATE** = small random offsets in `params` on top of current craft (escape hatch to non-grid territory).

## Materials (14) and their deltas

Deltas below are the design intent and starting values; final numbers are tuned by ear in Phase 4/6. Format: parameter ← change at weight 1.0.

| Material | Character | Primary deltas |
|---|---|---|
| ICE | cold, wide, long | uni_count +2, uni_detune +12c, release ×2.5, cutoff +25%, cave_mix +0.15, PW→38% |
| LAVA | hot, aggressive | crush_mix +0.3, crush_bits −6, filt_res +0.2, env2→cutoff +0.4, sub_level +0.2 |
| STONE | dry, blunt, raw | raw ON (weight≥1), release ×0.4, cave_mix 0, PW 50%, cutoff −15% |
| WOOD | warm, mellow | cutoff −35%, PW 47%, attack +8ms, vel_amp +0.2 |
| GLASS | thin, bright, delicate | PW→14%, cutoff +40%, dly_mix +0.2, level −0.1, attack 1ms |
| GOLD | expensive, wide, polished | uni_count +3, uni_spread +0.3, cave_mix +0.1, fine ±4c, master sheen (HP rumble cut) |
| CRYSTAL | metallic, singing | oscB_sync ON + oscB_oct +1, oscB_semi +7, env2_pitch +5st fast decay, cutoff +30% |
| VOLT | electric, jittery motion | lfo1_pwm +0.5, lfo1_rate→1/16, lfo2_dest=cutoff, lfo2_amt +0.3, lfo2_shape=s&h |
| SLIME | wobbly, gluey | glide +120ms, lfo2_dest=pw, lfo2_amt +0.4, lfo2_rate→1/8, PW→60% |
| TNT | percussive boom | env2_pitch +24st (downward drop), env2 decay 90ms, sustain 0, crush_mix +0.2, noise ON burst |
| MOSS | lo-fi, chill | crush_down +8×, crush_mix +0.25, cutoff −25%, lfo2 slow tri→pitch ±5c |
| SAND | gritty texture | noise ON, noise_level +0.35, noise_mode=long, filt_res +0.1 |
| OBSIDIAN | dark, heavy, deep | cutoff −55%, sub ON +0.3, oct −1 tendency, release ×1.5 |
| CLOUD | soft, airy, distant | attack +300ms, cave_mix +0.35, cave_size +0.3, level −0.15, cutoff −10% |

Conflict rule: deltas are applied in a fixed material order (table order), multiplicative for × entries, additive for the rest; discrete switches (raw, sync, noise) use "any material at weight ≥1 may switch ON; STONE's raw wins last".

## Per-cell WEIGHT / MIX

*Producer-requested for v1.0 (the architect had deferred it to v1.1 "Crafting 2.0"; the producer overrode that and specified the semantics below). Rationale, verbatim: sometimes a material is simply too much and you want to dial it back without removing the block.*

Every placed cell carries a **weight** in `0..1` (1.0 = 100%, the default), edited by a slider on the block itself. It scales that cell's contribution to its material's delta set — the cell contributes `weight` instead of a full copy.

| Delta class | Behaviour at weight *w* |
|---|---|
| **add** / **multiply** | copy *k* contributes `copyWeight(k) * w`, where the copies of a material are ordered by **descending** weight so the heaviest cell is always copy 0. Crafting therefore stays shapeless: only the multiset of (material, weight) pairs matters, never where the blocks sit. |
| **set** | weighted toward the target by the material's **strongest** cell weight: `value += wMax * (target - value)`, and exactly `value = target` at `wMax = 1`. Stacking still does not intensify a set. |
| **switch** (raw, sync, noise on, LFO dest/shape) | fires for **any** non-zero weight — a bool cannot be half on. Tempo-synced LFO **rates** count as switches on purpose, so a synced LFO never lands between note divisions; turning a material down reduces the modulation **depth** instead (`lfo1_pwm` / `lfo2_amt` are adds, so they scale). |
| **weight 0** | the material is **not there** for the delta math — bit-identical to leaving those cells empty. The blocks stay *placed* for everything below. |

The soft-knee stacking clamp is unchanged and runs on top of the weighted sums.

**Recipe detection, discovery and auto-naming ignore weights completely.** A recipe matches on base + material identity + position only, so a grid with the right blocks at 30% — or at 0% — still triggers the recipe and still receives its hand-tuned override patch. This is non-negotiable: it protects every already-discovered recipe and the whole hunt mechanic. The auto-name follows the same principle (name and recipe both describe what you *placed*; weights only shape how it sounds).

With every weight at 1.0 the arithmetic is **bit-identical** to the pre-weight engine, so the frozen craft golden hashes, the 128 factory presets and every existing user preset are unaffected. Preset/session JSON carries `craft.weights` only when a placed cell is off 1.0 — see `docs/SPEC.md` §Preset format for the format and the migration rule.

Stacking clamp: the accumulated delta on each continuous parameter is soft-kneed against the SPEC rail instead of hard-clamped — linear up to 75% of the headroom between the parameter's post-set reference value and the rail, then compressed so it approaches (never reaches) the rail. Copies 3–8 of a material therefore keep changing the sound monotonically instead of slamming into the parameter range. Integer parameters (unison count, octaves, crush bits/down) still round and hard-clamp — steps are inherent to them.

## Hidden recipes (position-sensitive)

After crafting, check the exact grid pattern against the recipe book. On match: play a discovery jingle, toast "★ RECIPE DISCOVERED: <NAME>", apply the recipe's hand-tuned override patch (better than the plain craft result), and log it to a persistent **Discoveries** page (n/16 found). Ship **16 recipes** in v1. Eight are defined here; design the remaining eight in Phase 4 following the same spirit:

| Pattern (base + placement) | Name |
|---|---|
| PAD + ICE across the top row | PERMAFROST |
| BASS + OBSIDIAN left+right, LAVA below | MAGMA FLOOR |
| PERC + TNT top+bottom, SAND left | QUARRY KICK |
| LEAD + CRYSTAL in all 4 corners | SHARDSTORM |
| KEYS + WOOD, MOSS, CLOUD in a column | FOREST LULLABY |
| CHIP + GOLD in all 4 edge-centers | MIDAS MODE |
| DRONE + CLOUD full ring (8 cells) | STRATOSPHERE |
| PLUCK + GLASS diagonal (3 cells) | ICICLE HARP |

Recipe book stored as data (JSON) so more can ship in updates ("Recipe Update" = free content marketing). Do not show recipe hints in the UI beyond the n/16 counter.

## Implementation notes

- Crafting engine is pure and host-independent: `CraftEngine::apply(base, cells) -> ParamSnapshot`, fully unit-testable (determinism test: hash of resulting snapshot per fixed grid must match golden values).
- Factory presets are expressed as `craft + minimal overrides` so the browser's mini-grid icons teach the system.
- UI: drag-and-drop blocks (also click-cell-then-click-palette for accessibility), right-click clears a cell, hovering a material shows a 3-word tooltip ("cold · wide · long").
