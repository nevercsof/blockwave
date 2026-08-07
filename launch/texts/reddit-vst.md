# r/vst

**Pre-flight:** check the sidebar. This sub is specifically about plugins, so a release post is on-topic by definition — the main risks are an account-age gate or a required flair.
**Why:** small, low-risk, exactly the right topic. Plain plugin-news voice, no storytelling.

---

**Title:** BLOCKWAVE 1.0 — free open-source square-wave synth (VST3/AU, macOS + Windows, GPLv3)

---

Released the first version of a synthesizer I've been building. Free, GPLv3, source on GitHub.

**What it is:** a synth with one waveform — the square. Two pulse oscillators with independent PW and hard sync, square sub, NES-style LFSR noise, polyBLEP anti-aliasing with a global RAW bypass for deliberate aliasing.

**The unusual part:** patches are built on a 3×3 bench rather than a knob panel. A base archetype in the centre, material blocks around it (14 of them), each applying a defined set of parameter deltas. Deterministic — same grid, same sound, every machine — with a per-block mix control. Sixteen exact patterns are hidden recipes with hand-tuned overrides; three published, thirteen not.

**Under the hood:** 67 automatable parameters, TPT state-variable filter (LP24/LP12/BP/HP), 2 envelopes with pitch-env amount, 2 tempo-syncable LFOs, unison to 8 with detune/spread, poly/mono/legato with glide, and crusher → ping-pong delay → dark FDN reverb, each with its own HP and LP.

**128 presets** across LEAD/BASS/PLUCK/PAD/KEYS/CHIP/PERC/FX, each one expressed as the bench that produced it.

**Formats:** macOS VST3 + AU + standalone (10.15+, universal), Windows VST3 (10/11 64-bit). 44.1–192 kHz, any buffer size. Passes pluginval at strictness 10 on both platforms.

Not code-signed — macOS needs `xattr -dr com.apple.quarantine` on the plugin, Windows may need Properties → Unblock. Instructions are on the page and in the zip.

https://nevercsof.github.io/blockwave · https://github.com/nevercsof/blockwave
