# BLOCKWAVE

**Every sound is a square.**

BLOCKWAVE is a free, open-source synthesizer with exactly one waveform: the square.
No saws, no sines, no wavetables, no sample library. Just pulse waves — and it turns
out that is enough for NES grit, icy PWM pads, rubber basses, glassy plucks and
cavernous drones. Constraint is the instrument.

The other half of the idea: **you don't tweak knobs, you craft sounds.** Drop a BASE
block in the middle of a 3×3 bench, surround it with material blocks — ICE, LAVA,
OBSIDIAN, VOLT — and BLOCKWAVE builds the patch for you. Same blocks, same sound,
every time, on every machine. And **16 exact block patterns are hidden recipes** that
unlock hand-tuned signature sounds. Nothing in the interface tells you what they are.
Find them, or trade them.

Free forever, GPLv3, no account, no nag screen, no upsell.

---

## Features

- **Square-only engine** — two pulse oscillators (independent pulse width, hard sync),
  a square sub, and an NES-style LFSR noise generator (still a square, technically).
  Anti-aliased with polyBLEP, plus a global **RAW** switch that turns the anti-aliasing
  off for authentic 8-bit dirt.
- **The CRAFT bench** — a 3×3 grid, 8 base archetypes, 14 materials, and a per-block
  MIX rail so you can dial a material back instead of removing it. DICE rolls a random
  bench, MUTATE nudges the result somewhere the grid alone cannot reach.
- **16 hidden recipes** — exact block patterns that trigger a discovery jingle, a
  toast, and a better-than-crafted patch. A counter in the CRAFT tab tracks how many
  you have found. Your discoveries are saved and survive restarts.
- **128 factory presets** across LEAD, BASS, PLUCK, PAD, KEYS, CHIP, PERC and FX —
  every one of them built from a bench you can look at, so the browser teaches you
  the system while you audition.
- **A full synth underneath** — 67 parameters on the TWEAK tab: TPT state-variable
  filter (LP24 / LP12 / BP / HP), two ADSR envelopes with a pitch-envelope amount,
  two tempo-syncable LFOs, unison up to 8 with detune and stereo spread, poly / mono /
  legato with glide, and an FX chain of bit-crusher, ping-pong delay and the dark
  CAVE reverb. Everything is host-automatable.
- **Pixel-art interface**, drawn from scratch, with a 100 / 125 / 150 / 175 / 200 %
  scale slider that stays crisp instead of blurry, and remembers your choice.

## Install

Download the zip for your platform from the
[Releases page](https://github.com/nevercsof/blockwave/releases). Each zip contains a
plain-text `INSTALL.txt` with the same steps, so you can follow them offline.

The binaries are **not code-signed**. That is what a free plug-in built by one person
looks like — it costs money per year to make the warnings go away. Both platforms need
one extra click or one pasted command the first time.

### macOS

| Format | Copy it to |
|---|---|
| `BLOCKWAVE.vst3` | `~/Library/Audio/Plug-Ins/VST3` |
| `BLOCKWAVE.component` (Audio Unit) | `~/Library/Audio/Plug-Ins/Components` |
| `BLOCKWAVE.app` (standalone) | `/Applications`, or anywhere |

Then clear the download quarantine flag, or your DAW will silently refuse to load it:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/BLOCKWAVE.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/BLOCKWAVE.component
```

Rescan plug-ins in your host. For the standalone app, **right-click it and choose
Open** the first time (double-clicking will only offer to move it to the Trash). On
macOS 15 and newer you may instead need System Settings → Privacy & Security →
**Open Anyway**.

### Windows

Copy the whole `BLOCKWAVE.vst3` **folder** into:

```
C:\Program Files\Common Files\VST3
```

Then rescan plug-ins in your host. If SmartScreen or your antivirus flags it, use
Properties → **Unblock** on the folder before copying, or add an exclusion.

## System requirements

|  | Minimum |
|---|---|
| macOS | 10.15 Catalina or newer, Intel or Apple Silicon. VST3, AU, Standalone. |
| Windows | Windows 10 / 11, 64-bit. VST3 only. |
| Host | Anything that loads VST3 or AU instruments — FL Studio, Ableton Live, Logic Pro, Reaper, Bitwig, Cubase, Studio One. Or no host at all: use the standalone. |
| CPU | Any mid-range CPU from the last decade. 16-voice polyphony, up to 8-way unison. |
| Sample rates | 44.1 kHz to 192 kHz, any buffer size, offline rendering included. |

Your presets, starred favourites, discovered recipes and UI scale live in
`Documents/BLOCKWAVE`. Deleting BLOCKWAVE never touches that folder.

## Manual

The full feature tour — every tab, every knob, and how crafting actually works — is in
**[docs/MANUAL.md](docs/MANUAL.md)**.

It does not contain any recipes. That is deliberate.

## Build from source

Requirements: CMake ≥ 3.22, Ninja, and a C++20 compiler (Xcode Command Line Tools on
macOS, MSVC 2022 on Windows). JUCE 8 is fetched automatically on first configure —
there is nothing else to install.

```bash
git clone https://github.com/nevercsof/blockwave.git
cd blockwave
scripts/build.sh          # macOS: VST3 + AU + Standalone into build/release
scripts/validate.sh       # optional: pluginval at strictness level 10
```

On Windows, `cmake --preset win-release && cmake --build --preset win-release --parallel`.

To roll your own distributable macOS zip: `scripts/package-macos.sh`.
Release mechanics are in [docs/RELEASE.md](docs/RELEASE.md).

## License

BLOCKWAVE is free software under the **GNU General Public License, version 3** — see
[LICENSE](LICENSE). You can use it in commercial music with no restrictions and no
royalties; the GPL governs the *code*, not the audio you make with it.

Copyright © 2026 Kirill Boyko. Third-party components: [THIRDPARTY.md](THIRDPARTY.md).

All artwork, the bitmap font and the sound design are original. BLOCKWAVE is not
affiliated with, endorsed by, or derived from any game or game publisher.

## For contributors

Design documents: [docs/SPEC.md](docs/SPEC.md) (product spec and the full parameter
table), [docs/CRAFT_GRID.md](docs/CRAFT_GRID.md) (crafting engine — **contains recipe
spoilers**), [docs/SOUND_DESIGN.md](docs/SOUND_DESIGN.md),
[docs/ROADMAP.md](docs/ROADMAP.md). Working rules: [CLAUDE.md](CLAUDE.md).
