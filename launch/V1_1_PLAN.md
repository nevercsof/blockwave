# v1.1 — "Recipe Update"

**Internal document.** Unlike everything else in `launch/`, this one names unpublished recipes freely. Do not paste from it.

## Why this shape

A recipe update is the cheapest content marketing a free plugin can have. It costs a JSON file and a version bump, it gives people who already installed it a reason to come back, and it gives outlets a reason to write a second time about something they already covered. It also feeds the thing that makes the plugin spread in the first place: people telling each other what they found.

**Mechanically it is three files:**

| File | Change |
|---|---|
| `data/recipes.json` | append the new entries |
| `CMakeLists.txt` | `VERSION 1.1.0` |
| `docs/RECIPE_NOTES.md` | design notes for the new four |

No engine change. `RecipeBook` is data-driven and the coverage test iterates whatever is in the book (`tests/CraftCoverageTests.h`), so adding recipes needs no test edits. The discovery counter reads `getNumRecipes()` and will say n/20 by itself.

## The four new recipes

Chosen to cover ground the existing sixteen do not. Current sixteen span: PAD ×3, LEAD ×3, BASS ×2, PLUCK ×2, PERC ×2, CHIP ×2, KEYS ×1, DRONE ×1. Under-served: **KEYS** (one), **DRONE** (one), and there is no recipe at all using **WOOD** or **STONE** as its defining material.

### 1. HEARTHSIDE — KEYS

```
 WOOD  |  WOOD  |  WOOD
   .   |  KEYS  |   .
   .   |  MOSS  |   .
```
Warm, close, slightly worn electric-piano-adjacent keys. WOOD's low cutoff and velocity sensitivity across the whole top row, MOSS underneath for tape wobble. Technique: hollow 50 % keys (bible #3) plus lo-fi haze (#11). The override should sharpen the attack WOOD softens and add just enough delay to place it in a room. Fills the biggest gap in the book — KEYS has one recipe and it is the airy FOREST LULLABY; this is its opposite.

### 2. QUARTZ VEIL — DRONE

```
 GLASS  |    .   | GLASS
   .    |  DRONE |   .
 CLOUD  |    .   | CLOUD
```
A high, still, glassy drone that sits above a mix rather than under it. GLASS in the top corners for thin bright pulses, CLOUD in the bottom corners for the long attack and cave. Technique: cave ambience (#13) at the top of the register instead of the bottom. Symmetric pattern, easy to stumble into, and DRONE currently has only STRATOSPHERE.

### 3. GRAVEL ROAD — PERC

```
 STONE |    .   | STONE
   .   |  PERC  |   .
 SAND  |  SAND  |  SAND
```
A dry, unglamorous, entirely un-reverbed percussion kit voice — the sound of hitting something in a room with no room. STONE's RAW switch and short release, SAND's noise across the bottom. Technique: chip drums (#8) with RAW dirt (#12). Deliberately the least pretty recipe in the book; the point is that it cuts through a mix where the polished ones do not. First recipe where STONE is the defining material.

### 4. SOLAR FLARE — LEAD

```
  GOLD  |  LAVA  |  GOLD
    .   |  LEAD  |   .
    .   |  VOLT  |   .
```
A wide, hot, moving anthem lead. GOLD's unison and spread, LAVA's crush and resonance between them, VOLT below for the S&H motion that keeps it from sitting still. Technique: supersquare (#4) plus electric jitter. The corners-plus-stem shape is distinct from every existing pattern. Gives LEAD a recipe that is neither a sync scream (SHARDSTORM, EMBER CROWN) nor a vocal (GEODE VOICE is a preset, not a recipe).

**Pattern collision check before shipping:** run the existing `recipe_book_coverage` test — it already asserts zero duplicate patterns and that every recipe is reachable with no shadowing. It will catch a collision automatically.

## What ships alongside

- Publish **one** more of the original sixteen at the same time, so the update rewards both new hunters and people who have been stuck. That keeps the published count at 4 of 20 and leaves sixteen unlisted.
- A short release note listing only that four new recipes exist — never what they are.

---

## Explicitly out of scope: first-finder credit in the About screen

The original Phase 8 brief proposed putting the name of the first person to find each recipe into the plugin's About screen. **Cut, on the producer's call, and the reasoning should be recorded so nobody re-proposes it.**

It converts a self-contained toy into an administered competition. Someone has to verify who was actually first, adjudicate screenshots and timestamps, decide what counts as proof, handle ties, ship a new build for each winner, and manage the people who believe they were cheated. Every one of those is an ongoing obligation with no end date, attached to a thing whose entire appeal is that it asks nothing of anyone and has nothing to sign up for.

The hunt works because it is a conversation between players, not a leaderboard they submit to. Leave it there.
