# RECIPES_FORMAT — the recipe book JSON (Phase 4)

Audience: **preset-designer** (hand-tune the 8 spec override patches, add 8 new
recipes) and **ui-engineer** (Discoveries page, mini-grid icons). The engine
side lives in `src/CraftEngine.h` (pure) + `src/CraftJson.h` (JSON adapter).

## File

`data/recipes.json`, embedded into the plugin via BinaryData (CMake target
`BlockwaveRecipeData`, header `RecipeData.h`, namespace `RecipeData`). The
same data is linked into `tools/render` so offline renders and the plugin
craft identically. Ship more recipes later by editing this one file.

## Schema

```json
{
  "formatVersion": 1,
  "recipes": [
    {
      "name": "PERMAFROST",                       // ALL CAPS, unique, shown in the toast
      "category": "PAD",                          // preset-browser category hint
      "base": "PAD",                              // one of LEAD BASS PAD PLUCK KEYS CHIP PERC DRONE
      "cells": ["ICE","ICE","ICE","","","","",""],// 8 outer cells, see indexing below
      "params": { "uni_count": 7, "cave_mix": 0.4 } // hand-tuned override patch
    }
  ]
}
```

- `cells` — material token per cell (`ICE LAVA STONE WOOD GLASS GOLD CRYSTAL
  VOLT SLIME TNT MOSS SAND OBSIDIAN CLOUD`), `""` = empty. **Cell indexing is
  frozen** (same as preset `craft.cells` and the UI grid): the 8 outer cells
  of the 3×3 in reading order, skipping the center base:

  ```
  0 1 2
  3 . 4        (. = BASE, center)
  5 6 7
  ```

- Matching is **position-sensitive and exact**: base equal, all 8 cells equal
  (empty cells must be empty). Checked AFTER normal crafting; first match in
  file order wins — keep patterns unique.

- `params` — a SPEC-table override object (identical semantics to preset
  `"params"`: frozen IDs, choices as strings, clamped to SPEC ranges). It is
  applied **on top of the plain craft result** for that grid, so tune it as a
  *diff*: start from what the grid already sounds like and push it to the
  signature sound. Recipes must sound clearly better than their plain-craft
  neighborhood (Phase 4 DoD).

- The override applies wherever the grid is crafted — interactive grid edits,
  preset loads, `tools/render` — so a factory preset that *is* a recipe needs
  few or no extra `params` of its own.

## Pattern freeze / sync rule

The 8 spec patterns are mirrored as pure C++ in
`src/CraftEngine.h::specRecipePatterns()` (used by the JUCE-free determinism
tests). The state test `craft_recipe_book` asserts JSON ⊇ those 8 patterns
verbatim — **do not edit the 8 spec patterns in either place**. New recipes
only need a JSON entry (plus, ideally, a pattern line in the checkpoint doc).

Two of the spec patterns needed a concrete reading (decided in Phase 4, now
frozen):
- FOREST LULLABY — "WOOD, MOSS, CLOUD in a column" = **left column, top to
  bottom** (cells 0, 3, 5).
- ICICLE HARP — "GLASS diagonal (3 cells)" = the grid's 3-cell diagonal
  TL–center–BR where the center is the PLUCK base itself, i.e. **GLASS at
  cells 0 and 7**.

## Designing the 8 new recipes (preset-designer)

Same spirit as the spec eight: a memorable material shape (row, ring, cross,
corners...) on a fitting base, a coined ALL-CAPS name (generic English words
only — legal rules in CLAUDE.md), and an override patch worth hunting for.
Avoid patterns a DICE roll hits too easily (single-cell patterns are
forbidden; prefer ≥ 3 filled cells or strict symmetry) and keep every pattern
distinct from the existing ones.

## Discoveries (ui-engineer)

- `DiscoveryStore` (`src/CraftJson.h`) persists found names to
  `~/Documents/BLOCKWAVE/Discoveries.json` (lazily created, message thread).
- Discovery registers only on **interactive grid edits**
  (`setCraftGrid`/dice), never on preset/session load — hunting stays a hunt.
- Poll `processor.consumeRecipeDiscovery(name)` from a UI timer for the toast
  + jingle; counter = `getDiscoveries().getNumFound()` of
  `getRecipeBook().getNumRecipes()` (n/16 once all 16 ship).
