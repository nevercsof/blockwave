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
| CRYSTAL | metallic, singing | oscB_sync ON + oscB_semi +7, env2_pitch +5st fast decay, cutoff +30% |
| VOLT | electric, jittery motion | lfo1_pwm +0.5, lfo1_rate→1/16, lfo2_dest=cutoff, lfo2_amt +0.3, lfo2_shape=s&h |
| SLIME | wobbly, gluey | glide +120ms, lfo2_dest=pw, lfo2_amt +0.4, lfo2_rate→1/8, PW→60% |
| TNT | percussive boom | env2_pitch −24st, env2 decay 90ms, sustain 0, crush_mix +0.2, noise ON burst |
| MOSS | lo-fi, chill | crush_down +8×, crush_mix +0.25, cutoff −25%, lfo2 slow tri→pitch ±5c |
| SAND | gritty texture | noise ON, noise_level +0.35, noise_mode=long, filt_res +0.1 |
| OBSIDIAN | dark, heavy, deep | cutoff −55%, sub ON +0.3, oct −1 tendency, release ×1.5 |
| CLOUD | soft, airy, distant | attack +300ms, cave_mix +0.35, cave_size +0.3, level −0.15, cutoff −10% |

Conflict rule: deltas are applied in a fixed material order (table order), multiplicative for × entries, additive-with-clamping for the rest; discrete switches (raw, sync, noise) use "any material at weight ≥1 may switch ON; STONE's raw wins last".

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
