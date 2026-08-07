# KVR Audio — product database listing

**Prerequisite (Kirill, ~10 min):** a free KVR Developer Account — https://www.kvraudio.com/developer_application.php. Nothing can be listed until it exists. Verified 2026-08-07.
**Then:** add the product in the database, and optionally submit a news item. Also worth requesting inclusion in the Free VST Plugin Mega List (https://www.kvraudio.com/the-vst-free-plugin-mega-list).
**Constraint that shaped this:** KVR is a database, not a blog. Fields, not prose.

---

## Fields

| Field | Value |
|---|---|
| Product name | BLOCKWAVE |
| Developer | Kirill Boyko |
| Version | 1.0.0 |
| Type | Instrument — Synth (subtractive / pulse) |
| Formats | VST3, Audio Unit, Standalone |
| Platforms | macOS 10.15+ (Intel + Apple Silicon), Windows 10/11 64-bit |
| Price | Free |
| Licence | GPLv3 (open source) |
| Copy protection | None |
| Released | 2026-08-07 |
| Product page | https://nevercsof.github.io/blockwave |
| Download | https://github.com/nevercsof/blockwave/releases/tag/v1.0.0 |
| Source | https://github.com/nevercsof/blockwave |
| Manual | https://github.com/nevercsof/blockwave/blob/main/docs/MANUAL.md |

## Short description (one line)

A square-wave-only synthesizer where you build patches by placing material blocks on a 3×3 grid.

## Long description

BLOCKWAVE is a free, open-source synthesizer built around a single constraint: every sound is a square wave. Two pulse oscillators with independent pulse width and hard sync, a square sub-oscillator, and an NES-style LFSR noise generator, anti-aliased with polyBLEP — plus a global RAW switch that turns the anti-aliasing off for authentic 8-bit dirt.

Instead of starting at a knob panel, you start at a bench. Place a base archetype in the centre of a 3×3 grid, surround it with material blocks — ICE, LAVA, STONE, WOOD, GLASS, GOLD, CRYSTAL, VOLT, SLIME, TNT, MOSS, SAND, OBSIDIAN, CLOUD — and BLOCKWAVE computes the patch. Crafting is deterministic: the same blocks produce the same sound on every machine. Each block has its own mix control, so a material can be dialled back rather than removed. DICE randomises the bench; MUTATE nudges the result somewhere the grid alone cannot reach.

Sixteen exact block patterns are secret recipes that unlock hand-tuned signature sounds, with an in-plugin discovery counter. Three are published; the rest are unlisted.

Underneath is a complete synthesizer: 67 host-automatable parameters, a TPT state-variable filter (LP24/LP12/BP/HP), two envelopes with a pitch-envelope amount, two tempo-syncable LFOs, unison up to eight voices with detune and stereo spread, poly/mono/legato with glide, and an FX chain of bit-crusher, tempo-synced ping-pong delay and a dark CAVE reverb — each with its own high- and low-pass filter.

128 factory presets across LEAD, BASS, PLUCK, PAD, KEYS, CHIP, PERC and FX. Every preset is expressed as the bench that made it, so the browser teaches the crafting system while you audition. Pixel-art interface drawn from scratch, with a 100–200 % scale slider.

No account, no registration, no email capture, no paid tier. GPLv3.

## Tags

synth, synthesizer, free, open source, chiptune, 8-bit, square wave, pulse, PWM, retro, lo-fi, VST3, AU, GPLv3
