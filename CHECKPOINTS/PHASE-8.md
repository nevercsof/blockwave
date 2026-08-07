# CHECKPOINT — Phase 8 (launch kit)

Date: 2026-08-07 · **RUN STOPS HERE.** Nothing has been posted, emailed or announced. Everything is prepared and verified; the launch is a human act and it is Kirill's to perform.

## Live now

| | |
|---|---|
| **Landing page** | **https://nevercsof.github.io/blockwave/** — GitHub Pages, source `main` / `/docs`, status `built`, HTTP 200 with all assets |
| **Discovery Board** | https://github.com/nevercsof/blockwave/discussions/1 — Discussions enabled, template posted |
| **Release** | https://github.com/nevercsof/blockwave/releases/tag/v1.0.0 — both zips 200 |

## Delivered

**Landing page** (`docs/index.html` + `docs/assets/`) — hero, the discovery GIF, macOS and Windows download buttons with the unsigned-build instructions **visible on the page without clicking** (verified by fetching the live page), the three published recipes as 3×3 diagrams, an honest feature list, footer. Self-contained: no build step, no external fonts, no trackers, no analytics, nothing to consent to.

**Press kit** (`launch/presskit/`) — the recipe-discovery **GIF** (100 KB, 832×456, driven through the real code path: setCraftGrid → recipe match → toast), five screenshots, a logo pack rendered from the product's own pixel font, `ONE_PAGER.md`, `DESCRIPTIONS.md` at 25/50/100/250 words, and a README that grants reuse explicitly so an outlet never has to ask.

**Outreach texts** (`launch/texts/`) — Bedroom Producers Blog pitch, KVR listing card, four Reddit posts each genuinely written for its sub, and the Show HN / r/programming engineering story.

**Also** — `launch/YOUTUBE_CHANNELS.md` (15 channels + one email template), `launch/RESEARCH_communities.md`, `launch/V1_1_PLAN.md`, `launch/LAUNCH_DAY.md`.

## Judgement calls — things deliberately NOT built

1. **First-finder credit in the About screen — cut.** Kirill's call and it is the right one. It converts a self-contained toy into an administered competition: someone must verify who was first, adjudicate screenshots, ship a build per winner and manage the people who feel cheated. An open-ended obligation attached to a thing whose whole appeal is that it asks nothing of anyone. Recorded in `V1_1_PLAN.md` so it is not re-proposed.
2. **Community "found so far" counter — cut**, same reasoning at smaller scale. The plugin already counts privately (n/16); a public counter is a number somebody has to keep true forever. The board works without it.
3. **Email signup — replaced, not built.** A real form needs a third-party provider and an account Kirill does not have. **GitHub Watch → Releases** is the primary path: it notifies people today, costs nothing, and leaves no personal data in anyone's custody. A Buttondown block sits commented out in the HTML with three lines of instructions if he ever wants it. Verified the placeholder is inside an HTML comment and not live markup.

## Honest gaps

1. **Reddit rules could not be read from here** — `reddit.com` and `old.reddit.com` both refuse automated fetching. I did **not** invent them. `RESEARCH_communities.md` instead gives a five-point sidebar pre-flight to run per sub (dedicated promo thread? account age? flair? 9:1 rule? link posts allowed?), and every Reddit text names the constraint to check before posting. Fabricated rules get accounts banned; a stated gap does not.
2. **YouTube business emails cannot be collected by anyone** — YouTube puts them behind a CAPTCHA by design. The list gives channels plus the exact route (About → View email address → CAPTCHA). No address is invented. Verified live: Bedroom Producers Blog's free-plugin hub and active 2026 roundup channels including Mack Beats Studio; every other channel is marked "verify it is alive before writing", which is one click.
3. **Pinning the Discovery Board needs one manual click** — `pinDiscussion` does not exist in GitHub's current GraphQL schema. Open the discussion → ⋯ → Pin discussion.
4. **KVR needs a developer account first** (~10 min, free) before the listing card can be submitted.

## Verified

- **Recipe secrecy:** the 13 unpublished names appear **zero times** across 16 public files, including image filenames. Only PERMAFROST, MAGMA FLOOR and QUARRY KICK are public, and all three render correctly on the live page. `V1_1_PLAN.md` is the one internal document that names things freely and is marked as such.
- **Brand:** zero Mojang/Minecraft-family terms anywhere in the launch material.
- **Links:** every URL in the landing page returns 200 except the deliberately-commented-out email placeholder. Both release zips download and match their local checksums.
- **Live page:** fetched and inspected after publishing — hero, GIF, both download buttons, all three sets of install instructions visible without interaction, three recipe grids, Watch/Atom in place of a mailing list.
- No code was touched: `src/` and `plugin/` unchanged apart from the screenshot tool additions that produce the GIF.

## Kirill's next steps, in order

1. **Pin** the Discovery Board discussion (one click).
2. Read `launch/LAUNCH_DAY.md`. It is one page and it is ordered for a reason.
3. Run the day-before pre-flight — in particular **install from the zip on your own Mac following your own instructions**. Unsigned builds are the number one reason a free plugin gets called broken.
4. Post when you feel like it. Nothing here expires.

## Backlog carried forward

- v1.1 "Recipe Update": four designed recipes (HEARTHSIDE, QUARTZ VEIL, GRAVEL ROAD, SOLAR FLARE) — a JSON file and a version bump, no engine change.
- Open questions still unanswered from PHASE-7: the deliberate tape-glide on abrupt tempo changes, the `loadPresetVar` float round-trip, and the pre-existing JUCE VST3 seal quirk.
- v1.x wish list: AU program list, lazy knob-strip pre-render, GOLD's "master sheen" (needs a parameter the frozen table has no room for).
