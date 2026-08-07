# Hacker News / r/programming — the engineering story

**Venue recommendation: Show HN.** The thing is free, runnable and open source, which is exactly what Show HN is for. r/programming rewards a written article over a repo link, so it is the second choice and only with the write-up below as the link target.

**Post this on a DIFFERENT DAY from the music-community posts, and do not cross-post it there.** Producers do not care who wrote the code; programmers do not care about pulse width. Mixing the two audiences makes both posts worse.

**One rule above all others: do not ask for upvotes anywhere.** HN enforces this socially and mechanically.

**Titles**
- Show HN: `Show HN: BLOCKWAVE – a synthesizer built by AI agents from a written spec`
- r/programming: `What actually broke when I had AI agents build a real audio plugin end to end`

**A note on framing.** "AI wrote my app" is a tired headline and will be judged harshly on both venues. The interesting content is the *specifics of what went wrong and what caught it*. Lead with the failure, not the achievement.

---

## Draft

I spent about a week producing a free VST/AU synthesizer, and I wrote almost none of the code. It was built by Claude Code agents working from a written specification, a roadmap with per-phase definitions of done, and four specialised subagents — DSP, UI, sound design, and QA. The result is real: 4947 automated tests, passes pluginval at strictness level 10 on macOS and Windows, 128 factory presets, and a synth engine with 67 automatable parameters that holds a 16-voice patch at about 3.5 % of one core.

That is the setup. The interesting part is what the process caught and what it did not.

**The QA agent passed a build the review agent then broke.** I ran two independent checks: a QA agent that builds, tests and validates, and an adversarial reviewer whose only instruction was to find where the code is still wrong. On one UI change, QA reported "clear for commit" — build clean, 4700+ tests green, pluginval SUCCESS — and the reviewer came back with five defects, two serious. Its method was not running tests. It computed screen coordinates by hand and traced where mouse events would actually land.

**The bug it found is a good one.** The plugin's UI scale control is a slider inside the component it rescales. Dragging it caused the window to oscillate, because each mouse-move applied a new scale, which moved the slider under the stationary cursor, which changed the value, which applied a new scale. That much was obvious once reported.

What was not obvious: the fix — defer the change to mouse-up — left a whole family open. Any scale change re-maps every control under a cursor that has not moved. So the *second* click of a double-click on the scale slider landed on the RAW toggle and silently changed an audio parameter. A slow wheel spin put the next notch on the master gain knob. A gesture about window size was changing the sound.

**Two attempted fixes both failed, in symmetric ways, and that is the actual lesson.** The first guard suppressed input for 350 ms after a scale change. The reviewer pointed out macOS's double-click interval defaults to 500 ms and reaches a second with accessibility settings on — beaten by a pause. The second guard suppressed input within 8 px of where the cursor was — beaten by a 9 px hand drift that never left the master knob. Both numbers were proxies for a sentence nobody had written down:

> An input event is stale iff the control it would actuate is not the control the user was aiming at when they last looked.

Encoded directly — compare the component under the pointer before and after the transform, arm only if it changed, anchor to the control that slid in, release when the pointer leaves it — every previous failure closed by construction. No clock, no radius, no threshold to tune. Three rounds to get from a real bug to the invariant that made it disappear.

I do not think an agent would have found that on its own. I do think a *second* agent, told to attack rather than verify, found it reliably, three times running. Verification asks "does it pass?" Adversarial review asks "where does it still break?" Those turn out to be very different questions, and only the second one found the audio parameter changing when you resized a window.

**Other things worth reporting.** The agents were disciplined about a real-time-audio rulebook — no allocation, no locks, no I/O on the audio thread — because it was written down as a constraint and tested with an allocator guard rather than left as advice. A resonance complaint from the human ("some presets sound harsh") produced a measurement tool that falsified the obvious hypothesis: filter resonance was not the cause; a bitcrusher placed after the filter was, its alias images pinned to a fixed frequency the filter could never touch. Two golden-file regenerations were caught and justified rather than rubber-stamped. And the process failed in dull ways too — agents stalled, hit rate limits mid-task, and once wrote a report whose arithmetic ("13 of 21 hashes changed") did not match its own correct list of names.

The synth is free and GPLv3 if you want to look at what came out: https://github.com/nevercsof/blockwave

Happy to answer questions about the process, including the parts that did not work.
