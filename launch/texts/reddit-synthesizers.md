# r/synthesizers — OPTIONAL, LOWEST PRIORITY

**Recommendation: post here last, or not at all.**

This sub skews hardware. A free software synth may be tolerated or may be straightforwardly off-topic, and I could not read their rules from here to check (Reddit blocks automated access — see `RESEARCH_communities.md`). **Read the sidebar before posting.** If software releases are discouraged, skip it: a removed post in a hardware-leaning community costs more goodwill than the reach is worth, and this is a quiet launch.

If you do post, lead with the synthesis, not the download. This audience cares about *how it makes sound*, and the honest technical angle is the only thing that earns attention here. Do not post the crafting-game framing first — it reads as gimmick to this crowd, and the engineering is genuinely more interesting to them.

---

**Title:** A synth with one waveform: what you can actually get out of squares and pulses only

---

I built a software synth restricted to a single oscillator family — square and pulse — partly to see where the ceiling is.

The voice: two pulse oscillators with independent pulse width (1–99 %) and hard sync, a square sub an octave or two down, and a 15-bit LFSR noise generator in the NES style, with long and short tap modes. Anti-aliasing is polyBLEP on both edges of the pulse, and on the sync reset, with a global bypass switch when the aliasing is the sound you want.

What surprised me in practice:

**Pulse width is the whole instrument.** At 50 % you get odd harmonics only — hollow, clarinet-adjacent. Move off it and the even harmonics return, and the timbre shifts continuously from woody to nasal to thin. Modulating it slowly gives you most of the perceptual width of two detuned oscillators from one.

**Hard sync recovers the formants** you give up by not having a saw. Sweeping the slave's tuning under sync produces the vowel-ish sweep that makes a square-only synth stop sounding like a square-only synth.

**LFSR noise is technically still square.** It is a 1-bit pulse train, so it doesn't violate the constraint, and the short-tap mode's 93-step loop is the metallic ring that gives you hats and zaps.

Filter is a TPT/ZDF state-variable (LP24/LP12/BP/HP). Two envelopes, one with a pitch amount for drum drops and mallet attacks. Two tempo-syncable LFOs. Unison to eight with detune and stereo spread. Crusher, tempo-synced ping-pong delay, and an eight-line FDN reverb, each with its own high- and low-pass.

Patching is done by placing material blocks on a 3×3 grid — deterministic, so a patch is describable as a short list of blocks — but the full parameter panel is there if you would rather work directly.

Free, open source, GPLv3. macOS VST3/AU/standalone, Windows VST3.

https://github.com/nevercsof/blockwave
