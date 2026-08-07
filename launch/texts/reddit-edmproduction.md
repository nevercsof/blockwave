# r/edmproduction

**Pre-flight — the important one.** This sub is large and historically strict about self-promotion. **Check for a dedicated weekly/monthly self-promo or "tools" thread before posting anything standalone.** If one exists, that is where this goes, and the text below shortens to the first three paragraphs. Also check the account-age and participation expectations.
**If in doubt, modmail first.** One short message costs a day and removes the risk entirely.
**Why the voice differs here:** this sub rewards posts that teach something. Lead with the idea, not the release.

---

**Title:** I built a synth around one constraint — every sound is a square wave — and made patching a crafting system

---

I wanted to see how far one waveform actually goes, so I built a synth that only has square and pulse waves. No saws, no wavetables, no samples. Two pulse oscillators with independent pulse width and hard sync, a square sub, and NES-style LFSR noise.

The short answer is: further than I expected. A square is odd harmonics only, which is the hollow woody sound everyone associates with chiptune — but move the pulse width off 50 % and you get even harmonics back, and sweeping that width is essentially free detuning. Stack unison on top and you get the square-wave answer to a supersaw. The 128 presets go from NES leads to lush PWM pads to sub-heavy basses without ever leaving the square family.

The part that took the longest was making patching approachable. Instead of a knob panel you get a 3×3 bench: put a base archetype in the middle, surround it with material blocks. ICE widens and lengthens and cools. OBSIDIAN drags everything down and dark. LAVA brings crush and resonance. VOLT adds jitter and motion. Fourteen materials, each a defined set of parameter deltas, stackable, and each with its own mix control if a material is too strong. It's deterministic — the same blocks always produce the same patch — so a "recipe" is a thing you can actually tell someone over text.

Which is why sixteen specific block patterns are hidden recipes that unlock hand-tuned versions of that sound. Three are published; the rest aren't documented anywhere. The plugin counts how many you've found.

There's a full synth underneath for when you just want to work — 67 automatable parameters, SVF filter, two envelopes with a pitch-envelope amount, two synced LFOs, unison, glide, and a crusher/delay/reverb chain with individual HP and LP on each. Every factory preset displays the bench that made it, so browsing presets teaches you the system.

It's free and open source, GPLv3. No account, no email, no paid version, no upsell. macOS gets VST3 + AU + standalone, Windows gets VST3.

**https://nevercsof.github.io/blockwave**

The builds aren't code-signed — there's a one-line command on the page to clear the macOS quarantine, and Windows may need Properties → Unblock. Skipping it is the one thing that makes it look broken.

If you try it, I'd rather hear what doesn't work than what does.
