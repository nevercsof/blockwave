# BLOCKWAVE — Manual

Everything BLOCKWAVE can do, in plain language. You do not need to know what a filter
is to read this; where a word is jargon, it is explained the first time it appears.

If you only read one section, read **[How crafting works](#how-crafting-works)**.

> This manual contains **no recipes**. Sixteen exact block patterns are hidden in
> BLOCKWAVE, and finding them is the point. Nothing here narrows the search.

**Contents**

1. [The idea](#the-idea)
2. [The top bar](#the-top-bar)
3. [The CRAFT tab](#the-craft-tab)
4. [How crafting works](#how-crafting-works)
5. [The TWEAK tab](#the-tweak-tab)
6. [The preset browser](#the-preset-browser)
7. [UI scaling](#ui-scaling)
8. [Where your files live](#where-your-files-live)
9. [Keyboard and mouse reference](#keyboard-and-mouse-reference)

---

## The idea

BLOCKWAVE makes one waveform: the **square wave**. A square wave is the simplest,
buzziest tone there is — it is what early game consoles and cheap toys used, and it
is the reason chiptune sounds like chiptune.

The trick is that a square wave is not one sound, it is a family. Change how much of
each cycle is "up" versus "down" — the **pulse width** — and it slides from a hollow
clarinet through a full buzz to a thin, nasal reed. Stack a few of them, detune them,
filter them, and you can get pads, basses, plucks and drones that do not sound like
chiptune at all.

So BLOCKWAVE has no saw wave, no sine, no wavetables and no samples. That is the whole
constraint, and everything below is built on top of it.

---

## The top bar

The strip across the top is always visible, whichever tab you are on. It is arranged
in three groups, left to right: **presets**, **sound**, **view**.

| Control | What it does |
|---|---|
| **BLOCKWAVE logo** | Just the logo. |
| **◀ / ▶** | Step to the previous / next preset in the bank, without opening the browser. The fastest way to audition your way through the library. |
| **Preset name** | Shows what is loaded. Click it to open the preset browser. |
| **PRESETS** | Also opens the preset browser. |
| **SAVE** | Opens the save panel: type a name, pick a category, save it as your own preset. Your bench and all 67 parameters are stored. |
| **★ (star)** | Marks the current preset as a favourite. Starred presets gather in the FAVORITES folder in the browser. Click again to unstar. Stars are remembered between sessions. |
| **RAW** | An LED-style on/off switch. See below. |
| **Master knob** | Final output volume, −60 dB to +6 dB. This is the same control as GAIN in the MASTER panel on the TWEAK tab — turning one moves the other. A gentle limiter sits after it so BLOCKWAVE cannot spit out a hard digital clip no matter what you do. |
| **SCALE slider** | Interface size: 100 / 125 / 150 / 175 / 200 %. See [UI scaling](#ui-scaling). |

### RAW

Digital synths have to work to keep a square wave clean. Played high up the keyboard,
the raw maths produces false extra tones — **aliasing** — a metallic, slightly
out-of-tune shimmer that sits under the note. BLOCKWAVE normally suppresses this.

**RAW turns the suppression off.** The result is exactly the sound of a cheap 8-bit
chip: grittier, dirtier, and wrong in a way that is often better. On basses and leads
it adds bite; on high, fast lines it adds sparkle and crunch. On a smooth pad it will
sound broken, which may also be what you want.

RAW is a single global switch and it is automatable, so you can flip it mid-track.

---

## The CRAFT tab

This is the home screen and the reason BLOCKWAVE exists. Instead of building a sound
by turning knobs, you build it by placing blocks.

### The bench

A **3×3 grid**. The centre cell holds the **BASE**; the eight cells around it hold
**MATERIALS**.

- **BASE (centre, required).** One of eight starting patches, each hand-tuned:
  `LEAD, BASS, PAD, PLUCK, KEYS, CHIP, PERC, DRONE`. This decides the broad shape of
  the sound — how it starts, how long it lasts, roughly where it sits. Change it with
  the ◀ ▶ arrows next to the bench, or by dropping a base block into the centre.
- **MATERIALS (the eight outer cells, optional).** Each one pushes the sound in a
  direction. You can leave cells empty, and you can place the same material more than
  once — a second copy pushes further, a third further still, with diminishing
  returns, so eight ICE blocks are colder than one but not eight times colder.

Beside the bench, an info panel shows the current BASE, how many blocks are on the
bench and whether a recipe is active, and carries the controls that act on the bench
as a whole: **UNDO / REDO** and **KEEP** (both below).

### The materials

Fourteen, in the palette down the right side. Hover one for a three-word summary.

| Material | Character | What it tends to do |
|---|---|---|
| **ICE** | cold, wide, long | Widens and detunes, opens the tone up, stretches the tail, thins the pulse. |
| **LAVA** | hot, molten, aggressive | Bit-crush and distortion-flavoured grit, more filter squeal, more low end. |
| **STONE** | dry, blunt, raw | Kills the reverb, shortens the tail, forces the plain square, switches RAW on. |
| **WOOD** | warm, mellow, round | Darkens the tone, softens the attack, makes it respond more to how hard you play. |
| **GLASS** | thin, bright, delicate | Very narrow pulse, bright and hollow, instant attack, a touch of delay. |
| **GOLD** | expensive, wide, polished | Big unison stack, wide stereo, a sheen on top and rumble trimmed off the bottom. |
| **CRYSTAL** | metallic, singing, sharp | Brings in the second oscillator hard-synced and tuned up — bell-like, singing. |
| **VOLT** | electric, jittery, motion | Fast, restless modulation: pulse-width wobble and a stepped random filter. |
| **SLIME** | wobbly, gluey, slow | Notes slide into each other; a slow wobble on the pulse width. |
| **TNT** | percussive, boom, drop | A pitch that drops on impact, a short body, a burst of noise. Drums live here. |
| **MOSS** | lo-fi, chill, dusty | Sample-rate reduction, darker tone, a slow drift in pitch. Dusty tape feel. |
| **SAND** | gritty, noisy, texture | Adds the noise generator into the mix, plus a bit of filter edge. |
| **OBSIDIAN** | dark, heavy, deep | Closes the filter a long way, brings in the sub oscillator, sits low. |
| **CLOUD** | soft, airy, distant | Slow fade-in, big reverb, pushed back and quieter. |

### Placing blocks

- **Drag** a material from the palette onto a cell.
- Or **click a cell, then click a material** — the same thing without dragging. Useful
  on a trackpad, and the status line at the bottom of the bench tells you which half
  of the move you are in.
- Or **click a material first** to arm it, then click any cell to place it.
- **Right-click a cell** to empty it.
- Every change re-crafts the sound instantly. Values glide over about 30 ms, so you
  can rearrange the bench while a note is held and never hear a click.

### MIX (per-block weight)

Every **filled** cell has a **weight** — how much of that material actually gets
applied, from 0 % to 100 %. Default is 100 %. The knob moves in 5 % steps so the
readouts stay round; an automation lane (see below) is continuous.

Use it when a material is right but too much. Half an OBSIDIAN is a sound you cannot
get any other way.

The control is hidden until you go looking for it, so the bench stays clean:

- **Hover a block.** A small **MIX** appears in its top-right corner.
- **Click MIX.** A knob opens on the block, bottom-right. Drag it up and down to set
  the weight; the percentage shows in the corner while it is open, and a big badge
  flashes up as you move it. **Click MIX again** to put the knob away.
- **Mouse wheel** over a block with its knob open nudges the weight by 5 %.
- **Keyboard: `M`** opens and closes the knob on the block you have focused, and
  **↑ / ↓** then move the weight (**Shift** for 25 % jumps).

Opening the knob costs you nothing else: **the rest of the block still drags** exactly
as before, so you can move a block around the bench with its knob open. Even the MIX
label itself will drag the block if you pull on it instead of clicking it. Right-click
still clears the cell wherever you click.

Whether the knob is open or not:

- The block **art darkens in five visible steps** as you turn it down, and anything
  below 100 % keeps a small `nn%` tag in the corner — so you can read a turned-down
  bench at a glance without touching anything.
- At **0 %** the material contributes nothing at all: sonically identical to an empty
  cell. The block stays placed, which matters, because —
- **Weights never affect recipes or the auto-generated name.** A pattern at 20 % is
  still that pattern. This is deliberate and permanent.

Empty cells have no MIX label and no knob. The knob is a view, not part of the sound:
it is never saved into a preset, and it closes when you clear the cell, drop a new
block on it, or load a preset.

#### Automating MIX

**Each block's mix is a host parameter, so you can automate it.** The eight are named
`craft_mix_1` … `craft_mix_8` and they follow the grid in reading order — top row left
to right, then the two middle cells, then the bottom row:

```
craft_mix_1  craft_mix_2  craft_mix_3
craft_mix_4       ·       craft_mix_5
craft_mix_6  craft_mix_7  craft_mix_8
```

Each runs 0–100 % with 100 % as the default. In FL Studio, open a block's knob, move
it, then right-click the automation target and use **last tweaked** as usual — or find
the parameter by name in the plugin's parameter list. Drawing a curve on `craft_mix_3`
fades that one material in and out over a bar, which is a very different gesture from a
filter sweep: you are not filtering the sound, you are removing an ingredient from it.

Two things worth knowing before you draw a lane:

- **Turning a mix is a macro.** A material's contribution is spread across many of the
  synth's parameters, so moving a mix moves those parameters too — you will see them
  respond in the host. That is the control working, not a glitch. It does mean a mix
  lane and a hand-drawn lane on, say, CUTOFF will fight over the same value; pick one.
- **The grid itself is not automatable**, and will not be. Which material sits in which
  cell is a structural choice, not a continuous one — and recipes are matched on
  placement, so a lane over it would make discoveries flicker in and out. Automate the
  mixes; place the blocks by hand.

Weights still never affect recipes or the auto-name, automated or not.

### The buttons

Three sit in a row under the material palette and rewrite the bench:

| Button | What it does |
|---|---|
| **DICE** | Fills random cells with random materials. The base is kept. This is the single best way to find sounds you would never have designed — roll it a dozen times and star the accidents. |
| **MUTATE** | Leaves the bench alone and applies small random offsets to the underlying parameters. It moves the sound somewhere the grid alone cannot reach. Press it repeatedly to drift further. |
| **CLEAR** | Empties every material cell. The base stays. |

Three more live in the bench info panel beside the grid:

| Button | What it does |
|---|---|
| **◀ / ▶ (base)** | Cycle the base archetype. |
| **UNDO / REDO** | Step back and forward through your bench edits. |
| **KEEP** | Save the bench as a preset and star it, in one click. |
| **DISCOVERIES** | Opens the discoveries page (top right). |

### UNDO and REDO

Crafting is meant to be poked at, so the bench remembers. **UNDO** steps back through
what you did to it and **REDO** walks forward again. Both are greyed out and dead when
there is nothing to step to, which is the only signpost you get and all you need.

**What counts as a step:**

- placing a block, clearing a cell, dragging a block between cells;
- cycling the base;
- moving a block's MIX knob;
- DICE, MUTATE and CLEAR.

A whole knob drag is **one** step, not one per pixel — and so is a run of wheel notches
or arrow-key nudges on the same block. You get back the state you started the gesture
from, not the value you passed through half a second ago.

MUTATE undoes properly, which is worth spelling out: two MUTATEs and one UNDO lands you
on the first mutation, not back on the plain crafted sound. Each step remembers the
whole patch, not just the blocks.

**What does not count:**

- **loading a preset.** Loading one clears the history. Undo will never drag you back
  into the parameters of the preset you were on before — if you load something and want
  your bench back, load your bench back. (KEEP, below, is how you make sure you can.)
- **anything on the TWEAK tab**, or a knob your host is automating. Both are already
  outside the bench: note that *any* bench edit recomputes the whole patch from the
  craft, so a hand tweak does not survive the next block you place either, with or
  without undo.

The stack holds **32 steps** and is per-session: it is never written into a preset or
your project, and it starts empty each time you open the editor.

There is a keyboard shortcut — **⌘Z / Ctrl+Z** to undo, **⌘⇧Z / Ctrl+Shift+Z** (or
**Ctrl+Y**) to redo — but treat it as a bonus. Many hosts grab those keys for their own
undo before a plugin ever sees them, so the buttons are the real control. Nothing else
on the bench changed: `M`, `F`, `DELETE` and the arrow keys all still do exactly what
they did.

### KEEP — starring a sound you just made

The star in the top bar and the stars in the browser work on **presets**. A sound you
just crafted, or that DICE handed you, is not a preset yet — so there is nothing to
star. **KEEP** is the bridge: one click saves the bench as a user preset *and* stars
it, and a gold `KEPT + STARRED` plate tells you what it was called.

- **The name** is whatever the bench is already showing: the recipe name if one is
  active, otherwise the auto-generated patch name ("FROZEN GOLDEN SLIMY BASS").
- **Collisions are handled silently.** DICE will hand you the same adjective pair
  again sooner than you would think, so a repeat is saved as `... 2`, `... 3` and so
  on rather than stopping to ask you a question or overwriting the earlier one. Names
  are made unique against the whole library, factory bank included.
- **The category comes from the base**: `LEAD → LEAD`, `BASS → BASS`, `PAD → PAD`,
  `PLUCK → PLUCK`, `KEYS → KEYS`, `CHIP → CHIP`, `PERC → PERC`, and `DRONE → FX`
  (there is no DRONE category, and the factory drones are filed under FX too).
- It shows up in the browser immediately, under **USER** and under **FAVORITES**, and
  it is on disk — it survives a restart like any other user preset.
- After a **MUTATE** the recipe name is gone on purpose, because the sound has left the
  recipe. KEEP follows that: a mutated patch is saved under its material name and never
  claims a recipe it no longer matches.

KEEP is greyed out until there is something on the bench. For everything else — naming
a sound yourself, saving a patch you built on the TWEAK tab — use **SAVE** in the top
bar as before.

### The patch name

Above the bench, in large letters, sits an automatically generated name built from the
materials you placed and the base — "Frozen Golden Lead", "Volatile Mossy Bass". It
changes as you build and it reads as a description, not a label. An empty bench reads
`UNCRAFTED`. It is a description of the bench, not the preset name — when you SAVE you
type whatever name you want, and when you press KEEP this is the name it uses.

### The keyboard strip

An 18-key (1.5 octave) clickable keyboard along the bottom. Click to audition without
touching your MIDI controller or drawing notes. C markers show the octaves. You can
also walk it from the computer keyboard once it has focus.

### The discoveries counter

The **DISCOVERIES** button shows how many of the 16 hidden recipes you have found:
`n/16`. Open it for a page listing the ones you have discovered, by name.

Undiscovered recipes show nothing at all — no silhouette, no hint, no "you are close".
That is the entire game. There are sixteen; they are found by experimenting with the
bench; nobody is going to tell you where they are.

When you hit one, you will know: a jingle plays, a `★ RECIPE DISCOVERED` toast slides
in with the name, and the patch snaps to a hand-tuned version that is noticeably better
than what the blocks alone would have produced. The count is saved permanently — you
never lose a discovery.

---

## How crafting works

The short version, for people who want to know what is actually happening.

**1. The base sets the starting point.** Each of the eight archetypes is a complete
patch — oscillators, filter, envelopes, effects, everything. Crafting always begins
from that snapshot.

**2. Each material applies a set of changes to it.** A material is nothing mystical:
it is a fixed list of parameter moves. ICE lengthens the release, raises the cutoff,
adds unison voices and detune, narrows the pulse. Those exact moves, every time.

**3. Stacking intensifies, with diminishing returns.** Two of the same material push
harder than one, but the second copy counts for half, the third for a quarter, and so
on. So you can lean on a material without the sound falling off a cliff. Changes are
also softly limited as they approach the edge of a parameter's range, which is why
copies five through eight still change the sound instead of flattening out.

**4. Position does not matter — except for recipes.** For ordinary crafting the bench
is *shapeless*: only which blocks are present matters, never where they sit. Two ICE
and a WOOD craft the same patch wherever you put them. Recipe detection is the one
exception — it looks at the exact pattern.

**5. It is completely deterministic.** The same bench produces the same sound on every
machine, forever. There is no randomness in crafting (DICE and MUTATE are random about
*what they place*, not about how crafting works). This is what makes recipes shareable:
a pattern that works for you works for everyone.

**6. Then you can tweak.** Crafting produces a normal patch. Switch to the TWEAK tab
and change anything you like; the bench does not fight you. Move a block afterwards
and the craft is recomputed from the base, though — so tweak last, or save first.

**Presets work the same way.** A factory preset is stored as *a bench plus a short list
of overrides*, which is why the browser can show you each preset's bench as a mini 3×3
icon. Looking at the sounds you like teaches you the material vocabulary faster than
any manual can.

---

## The TWEAK tab

The full synthesizer. **66 controls live here**, plus RAW in the top bar, for 67
parameters in total — every one of them automatable from your DAW.

Knobs are stepped and drawn as pixel art; drag up/down to change, and the value appears
in the readout under each knob. Most cells have a short hover tooltip.

The layout follows the signal path: sound sources on row 1, how notes behave and how
they are filtered on row 2, envelopes and modulation on row 3, effects on row 4.

### Row 1 — the sound sources

**OSC A** — the main oscillator. Its power switch is in the panel header.

| Control | What it does |
|---|---|
| **OCT** | Octave, −2 to +2. |
| **SEMI** | Semitones, −12 to +12. Tune it to an interval for instant harmony. |
| **FINE** | Fine tuning in cents, ±100. Small amounts against OSC B create slow, thick beating. |
| **PW** | **Pulse width**, 1–99 %. The core tone control of the whole synth. 50 % is the hollow, symmetric square; move away from it and the tone gets thinner and more nasal. Extremes are very thin and very quiet. |
| **LEVEL** | How much of it goes into the mix. |

**OSC B** — a second, identical oscillator, off by default. Same OCT / SEMI / FINE /
PW / LEVEL, plus:

| Control | What it does |
|---|---|
| **SYNC** | **Hard sync**: forces OSC B to restart every time OSC A completes a cycle. B then plays at A's pitch but with a torn, metallic character controlled by B's own tuning. Turn SYNC on, then sweep B's SEMI, for the classic screaming sync lead. |

**SUB** — a square oscillator one or two octaves below the note. Off by default.

| Control | What it does |
|---|---|
| **OCT** | −1 or −2 octaves. |
| **LEVEL** | How much low end to add. Small amounts glue a bass to the floor. |

**NOISE** — the noise source. It is not white noise from a random generator; it is an
**LFSR**, the shift-register noise a games console used — a pulse train that is
technically still a square. Off by default.

| Control | What it does |
|---|---|
| **MODE** | `long` = broad, hiss-like noise, good for snares, air and texture. `short` = a much shorter repeating pattern that sounds metallic and pitched, the classic console clang. |
| **LEVEL** | How much noise in the mix. |

### Row 2 — voicing and filter

**VOICE** — how notes are played and stacked.

| Control | What it does |
|---|---|
| **UNISON** | 1–8 copies of A and B per note, detuned against each other. This is how you get big. Costs CPU proportionally. |
| **DETUNE** | How far apart those copies are, 0–100 cents. Small = thick; large = seasick. |
| **SPREAD** | How wide the copies sit across the stereo field, 0–1. |
| **MODE** | `poly` = chords. `mono` = one note at a time, retriggering the envelopes. `legato` = one note at a time, but overlapping notes slide without retriggering — for expressive lead lines. |
| **VOICES** | Maximum simultaneous notes in poly mode, 1–16. When you run out, the oldest and quietest note is taken. |
| **GLIDE** | Time for the pitch to slide from the previous note to the new one, 0–2 s. Also called portamento. |
| **G.MODE** | `always` = every note slides. `legato` = only slides when notes overlap, so you choose per phrase. |

**FILTER** — removes part of the sound. The single most useful shaping tool in any
synth, and the difference between "buzzy square" and "musical".

| Control | What it does |
|---|---|
| **TYPE** | `LP24` = low-pass, steep — removes highs aggressively, the default and the workhorse. `LP12` = low-pass, gentler, keeps more air. `BP` = band-pass, keeps a slice in the middle, thin and telephone-like. `HP` = high-pass, removes lows, hollows the sound out. |
| **CUTOFF** | Where the filter acts, 20 Hz to 20 kHz. On a low-pass, low = dark and muffled, high = bright and open. The knob is the brightness knob. |
| **RES** | **Resonance**: emphasises the frequencies right at the cutoff point. A little adds a vocal quality; a lot adds a whistle that sings as you sweep. |
| **ENVAMT** | How much ENV2 moves the cutoff, −1 to +1. Positive opens the filter on each note, negative closes it. This is what makes plucks pluck. |
| **KEYTRK** | **Keyboard tracking**, 0–1. Makes the cutoff follow the note you play, so high notes stay as bright as low ones instead of disappearing. |

**LFO1 PWM** — a dedicated wobbler for pulse width. An **LFO** is a slow wave used to
move something automatically.

| Control | What it does |
|---|---|
| **RATE** | Speed. With SYNC on it reads as a note division (`1/1` down to `1/32`); with SYNC off, in Hz (0.01–40). |
| **SYNC** | Lock the rate to your project tempo. |
| **PWM** | Depth, 0–1. Sweeps the pulse width of A and B up and down. Slow and shallow gives a rich, breathing chorus out of a single oscillator — the classic PWM pad. Fast and deep is a warble. |

### Row 3 — envelopes and LFO2

An **envelope** describes how something changes over the life of a note: **A**ttack
(fade in), **D**ecay (fall to the sustain level), **S**ustain (the level it holds
while you keep the key down), **R**elease (fade out after you let go).

**ENV1 AMP** — the volume envelope. Every note is shaped by it.

| Control | What it does |
|---|---|
| **ATTACK** | 0–5 s. 0 = instant, clicky, percussive. Longer = a swell. |
| **DECAY** | 0–5 s. How fast it falls from the initial peak to the sustain level. |
| **SUSTAIN** | 0–1. The held level. Set it to 0 and the note dies on its own after the decay — that is a pluck. Set it to 1 and it holds forever — that is an organ. |
| **RELEASE** | 0–5 s. Tail after you let go. Long releases plus polyphony eat voices. |

**ENV2 MOD** — a second envelope that does not touch volume. It exists to move other
things: the filter cutoff (via FILTER → ENVAMT) and pitch.

| Control | What it does |
|---|---|
| **ATTACK / DECAY / SUSTAIN / RELEASE** | Same four stages, same ranges. |
| **PITCH** | How much ENV2 bends the pitch, ±48 semitones. Positive starts above and falls; negative starts below and rises. A big positive amount with a very short decay is a kick drum. Small amounts add a tiny blip of attack to plucks. |

**LFO2** — a general-purpose modulator you point wherever you like.

| Control | What it does |
|---|---|
| **RATE** | Speed, in note divisions when synced, otherwise 0.01–40 Hz. |
| **SYNC** | Lock to project tempo. |
| **SHAPE** | `square` = jumps between two values. `tri` = smooth up and down. `s&h` = **sample and hold**, a new random value at each step — the classic jittery, computery motion. |
| **AMOUNT** | Depth, −1 to +1. Negative inverts the direction. |
| **DEST** | What it moves: `pitch` (vibrato, or wild if deep), `cutoff` (filter wobble), `pw` (a second pulse-width wobble on top of LFO1), `vol` (tremolo). |

### Row 4 — the FX chain

The three effects run in this order — **CRUSH → DELAY → CAVE** — then the master stage.
Each has a **MIX** knob at 0 by default, so nothing is doing anything until you turn it
up.

Each effect also has **HP** and **LP** knobs. These filter *only what goes into that
effect*, never the dry signal — so you can, for example, keep a bass fundamental out of
the reverb while the harmonics still ring. At their extremes (HP at 20 Hz, LP at
20 kHz) they are fully off and cost nothing.

**CRUSH** — digital destruction.

| Control | What it does |
|---|---|
| **BITS** | Bit depth, 16 down to 1. Lower = coarser, noisier, more quantisation grit. |
| **DOWN** | Sample-rate divider, 1× to 64×. Higher = the aliasing, ringing, "old sampler" sound. |
| **HP** | Trims lows before the crusher, so the grit lands on the mids and highs. |
| **LP** | Tames the highs before the crusher — reduces the harshest fizz. |
| **MIX** | Blend, 0–1. |

**DELAY** — a tempo-locked echo.

| Control | What it does |
|---|---|
| **TIME** | Note division, `1/1` down to `1/32`, including dotted values (`1/4D` etc.). Always locked to your project tempo. |
| **FEEDB** | How much of the echo feeds back in, 0–0.9. High values give long trails. |
| **PING** | Ping-pong on/off: echoes alternate left and right instead of staying centred. |
| **HP** | Trims lows going into the delay line — the standard trick for echoes that do not muddy a bass. |
| **LP** | Darkens the echoes. Filtered once at the input, so repeats do not progressively disintegrate. |
| **MIX** | Blend, 0–1. |

**CAVE** — the reverb. It is deliberately dark and cavernous rather than a bright
studio plate.

| Control | What it does |
|---|---|
| **SIZE** | How big the space is, 0–1. Small = a stone room. Large = a canyon. |
| **DAMP** | How fast the highs die away in the tail, 0–1. High damping = a muffled, distant, underground space. |
| **HP** | Trims rumble out of the reverb input, keeping the low end tight while the space stays big. |
| **LP** | Softens the top of the reverb input for a darker, further-away tail. |
| **MIX** | Blend, 0–1. |

**MASTER**

| Control | What it does |
|---|---|
| **VELO** | How much your playing velocity affects volume, 0–1. 0 = every note the same level, like a console chip. 1 = fully expressive. |
| **GAIN** | Final output, −60 to +6 dB. The same parameter as the top-bar master knob. A transparent soft-clip ceiling sits after it. |

---

## The preset browser

Open it from the preset name or the **PRESETS** button in the top bar.

### Layout

A search field across the top, a folder tree on the left, the preset list on the
right. Every preset row shows its name and a **mini 3×3 bench icon** — the actual
blocks that made it. That icon is the best teaching tool in the plug-in: find a sound
you like, look at its bench, and you have learned something you can reuse.

### Search

Click the field at the top of the browser (or press **Ctrl/Cmd + F**) and start
typing. The list filters as you type.

- It matches **any part of the name**, not just the beginning — `ST` finds
  `STARLESS POND` and `DUST CURTAIN` alike — and it ignores capitals.
- It searches **everything you own**, both banks, whatever folder happens to be
  selected. The hits are still grouped under their category headings, and the folder
  tree greys out to show it is not doing anything right now.
- The field shows how many presets matched. Nothing matched reads
  `NO SOUND BY THAT NAME`.
- **Clear it** with the **X** in the field or with **Esc**. Esc on an empty field
  closes the browser, as it always did.
- **Clicking a folder clears the search** and hands the list back to that folder.
  Typing overrides the folder, touching a folder overrides the typing — whichever you
  did last is what you are looking at.

Only the search field takes plain letters, and only while it is the active area — so
**F** still stars the highlighted preset when you are in the list.

### Folders

| Folder | Contents |
|---|---|
| **★ FAVORITES** | Everything you have starred, from both banks, grouped by category, with a live count. Empty, it reads `STAR A PRESET TO PIN IT`. |
| **ALL** | Every preset that exists. |
| **FACTORY** | The 128 presets that ship with BLOCKWAVE, with a sub-folder per category: LEAD, BASS, PLUCK, PAD, KEYS, CHIP, PERC, FX. |
| **USER** | Everything you have saved, in the same category sub-folders. |

### Favourites

- Click the **★** on any row to star it **without loading it** — so you can sweep a
  folder and mark candidates as you go.
- Press **F** to star the highlighted row.
- The **★** in the top bar stars whatever is currently loaded.
- Stars survive restarts, re-scans and re-saves, and work for factory and user presets
  alike.
- To star a sound you just **crafted** — one that is not a preset yet, because you
  built it or DICE handed it to you — use **KEEP** on the CRAFT tab. It saves and
  stars in one click, and the result lands here.

### Navigating

- **↑ / ↓** move through the list; **Enter** loads.
- **Tab** cycles the three areas: search field → folder tree → list → search field
  (**Shift + Tab** goes the other way). **←** also jumps from the list to the tree,
  **→** or **Enter** goes back to the list.
- In the search field, **↑ / ↓ / Enter** drop you straight into the results.
- **Esc** clears the search; on an empty field it closes the browser.
- Outside the browser, the top-bar **◀ ▶** step through the whole bank.

### Saving your own

**SAVE** in the top bar opens the save panel. Type a name (the name of whatever is
currently loaded is offered as a starting point — clear it and type your own), pick a
category, confirm. Your preset is written
to your user folder as a small readable JSON file — the bench, the weights, and the
parameters. It appears in USER immediately, and you can star it like any other.

For a sound you just crafted and want to keep *now*, **KEEP** on the CRAFT tab does the
same thing in one click: it names the preset after the bench, files it by base, saves
it and stars it. SAVE is for when you want to choose the name yourself.

---

## UI scaling

The **SCALE** slider in the top bar has five stops: **100 / 125 / 150 / 175 / 200 %**.
It snaps, and the current value is shown in the chip next to it.

**The window resizes when you let go, not while you drag.** As you drag, the handle
and the readout follow your cursor and the readout turns orange to say "not applied
yet"; a small blue pip stays on the stop you are currently at, so you can see where
you are and where you are heading. Release, and the window changes size once. A spin
of the mouse wheel is likewise applied once, when the spin stops. The arrow keys move
one stop per press and apply straight away — except while you are holding the slider,
where they wait for you to let go, so two hands cannot fight over one control.

**After a resize, the spot under your cursor stays inert until you move the mouse.**
Everything just moved under the pointer, so anything that arrives at the place the
pointer was already sitting is aimed at a panel that is no longer there. Until you
move, that one spot cannot be clicked or scrolled by accident: the second click of a
double-click, and a follow-up wheel notch, can never land on the master knob, RAW,
SAVE, the star or the preset arrows. **This is not a timer.** However slowly you
double-click — including the slow settings under macOS Accessibility — and however
long you leave between wheel notches, the second one is still covered. A wheel notch
in that moment still counts as a scale step, so a slow spin keeps stepping the size,
one stop at a time, at any speed.

The moment you move the mouse you have aimed at the new layout, so everything is live
again immediately — there is nothing to wait out. And only that one spot is ever
inert: the preset list still scrolls, the tabs still work, and the keyboard is never
blocked.

**A wheel spin never resizes the window while you are holding another control.** If a
notch is still settling when you press and hold something else — a knob, a block, a
fader — the resize waits until you let go, rather than moving the panel out from under
a live drag.

BLOCKWAVE is pixel art, so it scales with nearest-neighbour rendering: pixels get
bigger, never blurry. 100 % and 200 % are pixel-perfect; the three stops in between
make some pixels one unit larger than others, which is a deliberate trade — chunky and
sharp beats smooth and smeared.

**Your choice is remembered globally.** Move the slider once and every new instance in
every new project comes up at that size. Existing projects reopen at whatever size they
were saved with, so an old session never resizes itself behind your back. If you never
touch the slider, BLOCKWAVE never writes a settings file.

---

## Where your files live

Everything BLOCKWAVE saves goes in one folder:

- **macOS**: `~/Documents/BLOCKWAVE`
- **Windows**: `%USERPROFILE%\Documents\BLOCKWAVE`

| File / folder | What it holds |
|---|---|
| `Presets/` | Your saved presets, one readable JSON file each. Back them up, share them, edit them in a text editor. |
| `Discoveries.json` | Which of the 16 recipes you have found. |
| `Favorites.json` | Your starred presets. |
| `Settings.json` | Your preferred UI scale. |

These files are created lazily — nothing exists until you actually save, star, discover
or rescale something. Uninstalling BLOCKWAVE never touches this folder.

---

## Keyboard and mouse reference

**CRAFT bench**

| Input | Action |
|---|---|
| Drag material → cell | Place it |
| Click cell, then click material | Place it (no dragging) |
| Click material, then click cell | Place it (armed material) |
| Right-click cell | Clear it |
| Hover a block | Reveal its **MIX** label, top-right |
| Click **MIX** | Open / hide that block's mix knob |
| Drag the knob | Set that block's mix, 0–100 % in 5 % steps |
| Drag anywhere else on the block | Move the block (works with the knob open) |
| Mouse wheel over a block with its knob open | Nudge its mix by 5 % |
| M | Open / hide the mix knob on the focused block |
| ↑ / ↓ | Adjust the mix when that block's knob is open (**Shift** = 25 % jumps), otherwise move a row |
| ← / → | Walk the whole bench in reading order, wrapping around |
| Delete / Backspace | Clear the focused cell |
| Return / Space | Place the armed material, select the cell, or cycle the base |
| Ctrl/Cmd + Z | Undo the last bench edit *(if your host lets the key through)* |
| Ctrl/Cmd + Shift + Z, Ctrl/Cmd + Y | Redo *(same caveat)* |

**Preset browser**

| Input | Action |
|---|---|
| Type (in the search field) | Filter the whole bank by name, live |
| Ctrl/Cmd + F | Jump to the search field |
| ↑ / ↓ | Move through the list |
| Enter | Load the highlighted preset |
| F | Star / unstar the highlighted preset (in the list) |
| Tab | Cycle search field → folder tree → list |
| ← | Jump from the list to the folder tree |
| → or Enter (in the tree) | Back to the preset list |
| Esc | Clear the search; on an empty field, close |

**Everywhere**

| Input | Action |
|---|---|
| Drag a knob up / down | Change its value |
| Hover anything | Tooltip |
| Esc | Close the open panel (browser, save, discoveries) |

---

## Getting help

Source, issue tracker and releases: <https://github.com/nevercsof/blockwave>

BLOCKWAVE is free software under the GNU General Public License v3. The music you make
with it is yours, unconditionally — the licence covers the code, not your audio.

Copyright © 2026 Kirill Boyko.
