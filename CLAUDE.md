# BLOCKWAVE — Project Constitution

Free, open-source (GPLv3) square-wave-only synthesizer. **Dev platform: macOS** (VST3 + AU + Standalone built and tested locally). **Windows x64 VST3 is built and pluginval-tested exclusively via GitHub Actions CI** (free for public repos) — never assume local Windows access. Primary host: FL Studio (macOS locally; Windows verified in CI + beta testers). Stack: **JUCE 8 + C++20 + CMake**. Working title BLOCKWAVE — treat as a placeholder constant (`JucePlugin_Name`), single point of change.

Read `docs/SPEC.md`, `docs/CRAFT_GRID.md`, `docs/SOUND_DESIGN.md`, `docs/ROADMAP.md` before any work. The roadmap is law: execute phases in order, meet every Definition of Done, then stop at the checkpoint.

## Team & delegation

Delegate by specialization to the subagents in `.claude/agents/`:
- `dsp-engineer` — oscillators, filters, envelopes, voice management, FX, performance.
- `ui-engineer` — JUCE components, pixel LookAndFeel, CRAFT grid UI.
- `preset-designer` — factory preset JSONs from `docs/SOUND_DESIGN.md`.
- `qa-runner` — builds, pluginval, offline render tests, regression. **Every phase must pass through qa-runner before its checkpoint.**

The human (Kirill) is the producer: he reviews checkpoints and answers taste questions. The architect (Claude in the chat app) reviews checkpoints and resolves design questions. Never silently expand scope: if a feature is not in SPEC, list it as a proposal in the checkpoint instead of building it.

## Real-time audio rules (non-negotiable)

On the audio thread (`processBlock` and anything it calls):
1. No heap allocation, no `new`/`delete`, no `std::vector` growth, no `String` construction.
2. No locks, no file/network I/O, no logging, no exceptions.
3. All buffers and voices allocated in `prepareToPlay`.
4. Parameter access via `std::atomic` / APVTS raw pointers; smooth audible parameters (`juce::SmoothedValue`, ~20–30 ms) — cutoff, levels, PW, mix knobs.
5. `juce::ScopedNoDenormals` at the top of `processBlock`.
6. Preset/craft changes are computed on the message thread and applied to the audio thread via atomic parameter writes only.
7. No static mutable state; multiple plugin instances must be fully independent.
8. Handle all sample rates (44.1k–192k) and buffer sizes (16–4096), including buffer size changing between calls and offline (faster-than-realtime) rendering.

## Quality gates (enforced by qa-runner)

- `pluginval --strictness-level 10` passes on every checkpoint build — locally on macOS AND on the Windows CI artifact.
- Warnings are errors: `-Wall -Wextra` (clang, local) and `/W4` (MSVC, CI).
- Headless render tool (`tools/render`) exists from Phase 1; every DSP feature gets an offline render test with basic spectral/level assertions.
- Parameter IDs are permanent after Phase 2 (host automation compatibility). Never rename or reorder released IDs.
- CPU: report per-voice cost each checkpoint; full 16-voice worst-case patch must stay comfortably realtime on a mid-range CPU.

## Legal & brand rules

- GPLv3 headers in every source file. Copyright: Kirill.
- Absolutely no Mojang/Minecraft assets, textures, fonts, coined names (e.g. no "Creeper", "Redstone", "Ender"). Generic English material words only (ICE, LAVA, STONE...). Original pixel art drawn from scratch. "Blocky/voxel aesthetic" is fine; imitation of specific Minecraft trade dress is not.
- Third-party code only if license-compatible with GPLv3; record everything in `THIRDPARTY.md`.

## Checkpoint protocol

At each phase end, write `CHECKPOINTS/PHASE-N.md` containing: summary of work, DoD status line-by-line, pluginval log path, build artifact paths, rendered audio/PNG artifact paths, CPU numbers, open questions (numbered), and proposals (out-of-scope ideas). Commit everything, then stop and wait for review.
