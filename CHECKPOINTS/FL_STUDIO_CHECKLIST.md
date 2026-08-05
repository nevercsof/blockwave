# FL STUDIO CHECKLIST — Kirill's Phase 7 pass

~20 minutes. Everything automatable has already been verified by machine (4709 checks, pluginval s10, 768 renders across 6 sample rates). **What is left is only what a machine cannot judge: does it load in a real host, does it feel right, does it sound right.**

Work top to bottom. For each item write **OK** or a short note. Anything you write a note on gets fixed before release — do not soften it.

Build under test: `build/release/BLOCKWAVE_artefacts/Release/` (VST3, AU, Standalone), version 1.0.0-rc1.

---

## A. Install and first load (5 items)

1. **Install the VST3.** Copy `build/release/BLOCKWAVE_artefacts/Release/VST3/BLOCKWAVE.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. (The AU is already installed there by `scripts/validate.sh`.)
2. Open FL Studio → **Options → Manage plugins → Find installed plugins**. Confirm **BLOCKWAVE** appears as an instrument/synth. → OK / note
3. Add it to a channel. **The editor opens without a hang or a blank window.** → OK / note
4. The **CRAFT tab is showing** (not TWEAK), the top bar reads a preset name, and the pixel art is crisp (no blur, no half-pixels). → OK / note
5. Play a few notes from your MIDI keyboard. **Sound comes out, in tune, no crackle.** → OK / note

## B. The core loop — does crafting feel good? (6 items)

6. **Drag a block** from the palette into a bench cell. The name plate changes, the sound changes. → OK / note
7. **Click a cell, then click a material** (the no-drag path). Same result. → OK / note
8. **Right-click a cell** — it clears. → OK / note
9. **Drag vertically on a filled block** — the block darkens, a weight readout appears, and the material's effect gets weaker. Then take it back to 100%. **Does this feel intuitive, or does it fight the drag-to-move?** → OK / note
10. **DICE** a few times. **MUTATE** a few times. Anything crash, click, or produce silence? → OK / note
11. **Find a recipe:** base **PAD**, then **ICE in all three top cells**. You should get a toast, a jingle, the name turns gold, and the counter goes to 1/16. → OK / note

## C. Playing it like an instrument (5 items)

12. **Hold a chord** (4+ notes) on a PAD preset. **Listen for the distortion you reported earlier — it should be gone.** → OK / note
13. Play the **keyboard strip at the bottom of the CRAFT tab** with the mouse. Notes sound, no stuck notes. → OK / note
14. **Play high** (C6+) on a few LEAD and CHIP presets. **Anything still painfully sharp?** Name it if so. → OK / note
15. Turn **CRUSH DOWN** up on any preset, then use the new **CRUSH LP** to tame the metallic whistle. Does LP do what you expect? → OK / note
16. Play a bass preset with reverb (TWIN RAIL, UNDERTOW, STARLESS POND). **Is the low end clean now?** → OK / note

## D. The browser (4 items)

17. Open **PRESETS**. Click through the folder tree — FAVORITES, ALL, FACTORY, each category, USER. Counts look right, list filters correctly. → OK / note
18. **Star 3–4 presets.** They appear under FAVORITES. Clicking the star does **not** load the preset. → OK / note
19. Load ~15 presets in a row quickly, **while a note is held**. No crash, no stuck note, no burst of noise. → OK / note
20. **SAVE** a patch of your own with a name. It appears under USER and reloads correctly. → OK / note

## E. Host integration — the part that breaks in real DAWs (7 items)

21. **Automate `filt_cutoff`** from FL's automation clip. Draw a fast sweep. **No zipper noise, no clicks.** → OK / note
22. Automate **`oscA_pw`** the same way. → OK / note
23. **Save the FL project. Close it. Reopen it.** The preset, the craft bench, the star, and the UI scale all come back exactly as they were. → OK / note
24. **Four instances** on four channels, all playing at once. CPU sane, no cross-talk, each keeps its own patch. → OK / note
25. **Change the project tempo** while a preset with delay is playing (try 120 → 90 → 160). The echoes re-lock. *Known: a fast tempo jump makes the delay glide like tape — tell me if it bothers you, it is a deliberate design choice we can change.* → OK / note
26. **Offline render** a short pattern to WAV (File → Export). The file matches what you heard. → OK / note
27. **Change the audio buffer size** (Options → Audio settings) between 64 and 2048 while playing. No dropouts beyond the normal glitch of switching, no crash. → OK / note

## F. Sample rates (2 items)

28. Switch the project to **96 kHz** (Audio settings). Everything still plays in tune and sounds the same. → OK / note
29. Back to **44.1 kHz**. Same. → OK / note

## G. The standalone (2 items)

30. Open `BLOCKWAVE.app`. Pick your audio device in **Options → Audio/MIDI Settings**. Play for ~30 s. **Listen specifically for crackle on silence** — this was never verified by ear. → OK / note
31. Resize the UI through all five scale steps (100 → 200%). Crisp at every step, window size sane on your display. → OK / note

---

## Taste questions (no right answer — your call)

- **T1.** Per-block weight: is vertical-drag the right gesture, or would you rather have a dedicated small slider on the block edge?
- **T2.** The scale slider: are 5 steps right, or do you want more/fewer?
- **T3.** POWER PELLET — keep the name or rename? (Pac-Man association; legally fine, purely taste.)
- **T4.** BOULDER ROLL — you marked it "?" during the cull. Keep it in the bank or swap for PIT STOMP?
- **T5.** Anything in the 128 that you would cut now that you have lived with it?

---

## How to report back

Just paste the numbers you wrote notes on, e.g.:

```
9 — drag fights the move, annoying
14 — MIDAS MODE still sharp at C6
23 — scale came back at 100% not 150%
T3 — rename to PIXEL PELLET
rest OK
```

Anything you flag gets fixed before we cut the release.
