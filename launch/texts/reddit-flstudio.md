# r/FL_Studio

**Pre-flight (do this first, see RESEARCH_communities.md):** check the sidebar for a dedicated self-promo/release thread, flair requirements, and any account-age minimum. If a release thread exists, this goes there instead of a standalone post.
**Why this sub first:** FL is the primary tested host and it is Kirill's home turf. Casual, practical voice. Lead with what it does, not with the philosophy.

**Flair:** whatever the sub uses for a tool/release, if flair is required.

---

**Title:** I made a free synth where you build sounds by placing blocks on a grid instead of turning knobs

---

I've been building a synthesizer for the last while and it's finally done. It's free and open source, no email, no account, nothing to buy.

The idea: every sound is a square wave. That's the whole oscillator section — two pulse oscillators, a square sub, and NES-style noise. Sounds limiting, and it is, but that's the point: you end up somewhere different than you would with another wavetable synth.

The part I'm actually happy with is how you make patches. There's a 3×3 bench. You put a base in the middle — LEAD, BASS, PAD, PERC and so on — and then surround it with material blocks: ICE, LAVA, OBSIDIAN, VOLT, MOSS, TNT, fourteen of them. Each material does a specific thing to the patch, so ICE widens and lengthens it, OBSIDIAN drags it down and darkens it, LAVA adds crush and bite. Stack the same block twice for more of it, or pull its mix down if it's too much.

It's fully deterministic, so the same blocks always make the same sound, on any machine. Which means recipes are shareable — and there are sixteen exact block patterns that trigger hand-tuned signature sounds with a little discovery jingle. Three are published, the other thirteen aren't. I'm curious how long they last.

There's a normal synth under it too if you'd rather just work: 67 automatable parameters, SVF filter, two envelopes with pitch-env, two synced LFOs, unison to 8, and crusher/delay/reverb each with their own HP and LP. 128 presets, and every preset shows you the bench that made it, so the browser is kind of a tutorial.

Tested mainly in FL on macOS. Windows VST3 is built and validated in CI but I don't own a Windows machine, so if something's off there I'd genuinely like to know.

**Download:** https://nevercsof.github.io/blockwave
**Source:** https://github.com/nevercsof/blockwave

One heads-up: the builds aren't code-signed (that costs money per year), so macOS will quarantine them. There's a one-line `xattr` command on the download page — takes five seconds, but your DAW will silently refuse to load the plugin if you skip it. Windows may want Properties → Unblock.

Happy to answer anything.
