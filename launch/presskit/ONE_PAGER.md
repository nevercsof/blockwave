# BLOCKWAVE — facts sheet

| | |
|---|---|
| **Name** | BLOCKWAVE |
| **One line** | A square-wave-only synthesizer where you build patches by placing material blocks on a 3×3 grid. |
| **Version** | 1.0.0 — released 7 August 2026 |
| **Price** | Free. No account, no email capture, no paid tier, no upsell. |
| **Licence** | GPLv3, source public |
| **Developer** | Kirill Boyko |
| **Platforms** | macOS 10.15+ (Intel + Apple Silicon) · Windows 10/11 64-bit |
| **Formats** | macOS: VST3, Audio Unit, Standalone · Windows: VST3 |
| **Signed?** | No — one `xattr` command on macOS, Properties → Unblock on Windows. Instructions ship in the zip and on the page. |

## The differentiator

Every other free synth gives you a knob panel. This one gives you a bench: a base archetype in the middle of a 3×3 grid, material blocks around it, and the patch is computed. Deterministic — the same blocks make the same sound on any machine — so a patch is something you can describe to someone in a sentence. **Sixteen exact block patterns are secret recipes** that unlock hand-tuned signature sounds. Three are published; thirteen are not.

## Numbers

| | |
|---|---|
| Factory presets | **128** (LEAD 24, BASS 20, PLUCK 16, PAD 16, KEYS 12, CHIP 16, PERC 12, FX 12) |
| Automatable parameters | **67** |
| Materials | **14** (ICE, LAVA, STONE, WOOD, GLASS, GOLD, CRYSTAL, VOLT, SLIME, TNT, MOSS, SAND, OBSIDIAN, CLOUD) |
| Base archetypes | **8** (LEAD, BASS, PAD, PLUCK, KEYS, CHIP, PERC, DRONE) |
| Secret recipes | **16** — 3 published, 13 unlisted |
| Polyphony | 16 voices, up to 8-way unison |
| CPU | ~3.5 % of one core, worst case (16 voices × 8 unison, all FX), 44.1 kHz / 128 |
| Sample rates | 44.1–192 kHz, any buffer size, offline render |
| Automated tests | 4947 · pluginval strictness 10 green on both platforms |

## Signal path

Two pulse oscillators (independent pulse width 1–99 %, hard sync) + square sub + NES-style 15-bit LFSR noise → TPT state-variable filter (LP24 / LP12 / BP / HP) → amp envelope → bit-crusher → tempo-synced ping-pong delay → dark FDN reverb → master soft-clip. polyBLEP anti-aliasing throughout, with a global RAW switch that turns it off. Each FX has its own high- and low-pass.

## What to try first

Load **FROSTBYTE** (PAD). Then open the CRAFT tab, clear the bench, set the base to **PAD** and put an **ICE** block in all three cells of the top row. That is one of the three published recipes and it announces itself.

## Links

- Site and download — https://nevercsof.github.io/blockwave
- Release — https://github.com/nevercsof/blockwave/releases/tag/v1.0.0
- Source — https://github.com/nevercsof/blockwave
- Manual — https://github.com/nevercsof/blockwave/blob/main/docs/MANUAL.md
- Press kit — https://github.com/nevercsof/blockwave/tree/main/launch/presskit

## Legal

All artwork, the bitmap font and the sound design are original to this project. BLOCKWAVE is not affiliated with, endorsed by, or derived from any game or game publisher.
