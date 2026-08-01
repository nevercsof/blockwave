# ROADMAP — phases & Definition of Done

Execute in order. A phase is complete only when every DoD item is verifiably true and qa-runner has signed off. End each phase with `CHECKPOINTS/PHASE-N.md` (protocol in CLAUDE.md) and stop for human/architect review.

## Phase 0 — Toolchain & skeleton
Repo layout (`src/ plugin/ tools/ presets/ art/ tests/`), JUCE 8 via CMake FetchContent, CMake presets for Release/Debug, empty plugin builds as VST3 + AU + Standalone on macOS, pluginval wired into `scripts/validate.sh`, **GitHub Actions workflow: macOS + Windows matrix build, pluginval s10 on both, artifacts uploaded per push**, GPLv3 LICENSE + file headers, `.gitignore`, `THIRDPARTY.md`.
**DoD:** clean build from scratch in one documented command on macOS; CI green with Windows VST3 artifact downloadable; pluginval strictness 10 passes on the empty shell on both platforms; standalone launches and passes audio through silence without crackles.

## Phase 1 — DSP core + headless renderer
PolyBLEP pulse oscillator (A/B/SUB) with PW + hard sync, LFSR noise (long/short), TPT SVF filter, 2× ADSR, 2× LFO (tempo sync), voice manager (poly/mono/legato, glide, stealing = oldest-quietest), unison with detune/spread, RAW bypass, velocity. `tools/render` CLI: `render preset.json input.mid out.wav`.
**DoD:** render tests green (pitch accuracy ±1 cent; aliasing report — FFT of naive vs polyBLEP square at C7 showing suppressed alias partials; sync/noise/env golden renders); no allocations on audio thread (verify with a debug allocator assert in processBlock); pluginval s10; CPU numbers reported for 16-voice × 8-unison worst case at 44.1k/128.

## Phase 2 — Parameters & preset system
Full APVTS per SPEC table (IDs frozen after this phase), JSON preset save/load, factory bank embedding via BinaryData, user preset folder, preset browser data model, state save/restore in host session.
**DoD:** host session state round-trip test; automation of cutoff/PW from host is click-free (smoothing verified by render); 8 temporary dev presets load correctly; pluginval s10.

## Phase 3 — Pixel UI (TWEAK tab + top bar)
Custom LookAndFeel (blocks, bevels, stepped knobs, bitmap font, palette from SPEC), TWEAK tab with all sections, top bar, preset browser UI with category groups, 1x/2x scaling.
**DoD:** offscreen-rendered PNG screenshots of every screen attached to checkpoint; all params reachable and readable; UI thread never blocks audio; resize/scale glitch-free; pluginval s10 including GUI tests.

## Phase 4 — CRAFT grid
CraftEngine (pure, deterministic), material delta tables, recipe detection (16 recipes: 8 from spec + 8 new designed and documented), Discoveries persistence, CRAFT tab UI (drag-drop, DICE, MUTATE, auto-naming, keyboard strip), browser mini-recipe icons.
**DoD:** determinism unit test with golden hashes; every material audibly distinct on every base (render matrix 14×8 attached as audio grid); all 16 recipes trigger and sound clearly better than their plain-craft neighborhood; discovery toast + jingle work.

## Phase 5 — FX block
Crush (bits + downsample + mix), tempo-synced ping-pong delay, CAVE reverb (dark algorithmic, e.g. FDN — must sound cavernous, not plate-y), master softclip ceiling.
**DoD:** bypass null tests; delay locks to host tempo incl. tempo changes; reverb tail survives note-off and transport stop; FX add ≤ agreed CPU budget; pluginval s10.

## Phase 6 — Preset factory (with human loop)
Run the SOUND_DESIGN pipeline: ~300 candidates → auto-QC → render pack for Kirill → cull to 128 → loudness-match → lock factory bank.
**DoD:** 128 presets shipped meeting category quotas; every preset = recipe + minimal overrides; QC report attached; Kirill has approved the final bank by ear.

## Phase 7 — QA, hardening, installer
FL Studio manual checklist executed on Kirill's Mac (numbered click-by-click list: load, play, automate cutoff+PW, save/reopen project, 4 instances, offline render, tempo change, buffer 64→2048, 44.1/48/96k), fix everything found. Windows: CI pluginval + a small public beta round (recruit 3–5 testers from FL communities; provide them the checklist). Packaging: macOS .pkg or zip with unsigned-install instructions (right-click→Open / xattr note), Windows zip/Inno installer built in CI. Versioning, README/manual with recipe-free feature tour.
**DoD:** checklist 100% pass on Kirill's Mac; Windows beta feedback resolved; installers/zips verified (macOS locally, Windows by a beta tester); zero known crashes; final pluginval s10 logs (both platforms) archived.

## Phase 8 — Launch kit
Landing copy + free-download flow with email capture, press kit (screenshots, GIF of a recipe discovery, logo pack), demo video script (beat: craft a patch on camera → discover a recipe → play a bar), draft posts for Bedroom Producers Blog pitch / KVR listing / r/edmproduction / r/FL_Studio, "seed the hunt" plan: publish 3 recipes, keep 13 hidden.
**DoD:** all assets in `launch/`; Kirill has a one-page launch-day checklist; demo beat recorded by Kirill using only BLOCKWAVE (his task, flagged in checkpoint).
