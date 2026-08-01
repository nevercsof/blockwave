# BLOCKWAVE

A free, open-source (GPLv3) synthesizer where **every sound is a square** — from NES grit to icy PWM pads. Beginners craft patches by placing material blocks on a 3×3 grid; hidden recipes unlock signature sounds.

**Status:** in development. Working title `BLOCKWAVE` is a placeholder (single constant in `CMakeLists.txt`).

## Formats

- macOS: VST3, AU, Standalone (built & tested locally)
- Windows x64: VST3 (built & pluginval-tested in GitHub Actions CI)

## Build (macOS)

Requirements: Xcode Command Line Tools, CMake ≥ 3.22, Ninja.

```bash
scripts/build.sh
```

Validate (pluginval strictness 10):

```bash
scripts/validate.sh
```

JUCE 8 is fetched automatically via CMake FetchContent on first configure.

## Docs

Start with [docs/SPEC.md](docs/SPEC.md), [docs/CRAFT_GRID.md](docs/CRAFT_GRID.md), [docs/SOUND_DESIGN.md](docs/SOUND_DESIGN.md), [docs/ROADMAP.md](docs/ROADMAP.md). Project rules: [CLAUDE.md](CLAUDE.md).

## License

GPLv3 — see [LICENSE](LICENSE). Copyright (C) 2026 Kirill Boyko.
Third-party components: [THIRDPARTY.md](THIRDPARTY.md).
