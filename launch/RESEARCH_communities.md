# Community rules research

**Honesty note.** Reddit blocks automated fetching from this environment — `reddit.com` and `old.reddit.com` both refused. I did **not** guess their rules and nothing below claims to quote them. For the four subreddits you get a **pre-flight you run yourself in 60 seconds per sub**, plus what to look for. KVR and Bedroom Producers Blog were verified live and are quoted with URLs.

Fabricated rules get accounts banned, so the honest gap is left as a gap.

---

## Verified live

### Bedroom Producers Blog — ✅ verified 2026-08-07

- Contact form with a dedicated **"Content tip"** category, described as *"if you found a new free plugin, sound library, etc."* — that is exactly our case.
- Direct email listed: **tomislav@bedroomproducersblog.com**
- Source: https://bedroomproducersblog.com/contact/
- They actively maintain a "Free VST Plugins" roundup page (https://bedroomproducersblog.com/free-vst-plugins/), updated through the year — being added to that page is worth as much as a news post.
- **Verdict: pitch them.** Use the contact form's Content tip category first; email is the fallback. Draft in `texts/bedroom-producers-blog.md`.

### KVR Audio — ✅ verified 2026-08-07

- Listing a product requires a free **KVR Developer Account**: https://www.kvraudio.com/developer_application.php
- With it you can *"add new products and manage their existing product listings in the KVR Product Database, submit news items for publication in the KVR News Sections"*.
- The database explicitly supports plugins and stand-alone apps — we are both.
- General submissions info: https://www.kvraudio.com/submissions
- There is also a **Free VST Plugin Mega List**: https://www.kvraudio.com/the-vst-free-plugin-mega-list
- **Verdict: do it, but it needs an account first** — that is a Kirill task, roughly ten minutes, and it gates the listing. Card prepared in `texts/kvr-listing.md`.

---

## Not verifiable from here — run this pre-flight yourself

Reddit's rules change and are per-sub. **Before each post, open the sub, read the sidebar and the rules page, and check these five things.** It takes a minute and it is the difference between a post and a ban.

| # | What to check | Why it bites |
|---|---|---|
| 1 | Is there a **dedicated self-promo / release thread** (weekly or monthly)? | Many production subs funnel *all* tool announcements into one thread. Posting standalone when a thread exists is the most common removal reason. |
| 2 | Is there an **account age or karma minimum**? | Automod silently removes posts from new or low-karma accounts. If Kirill's account is fresh, this is the blocker. |
| 3 | Is **flair required**, and which one? | Automod removes unflaired posts in many subs. |
| 4 | Is there a **9:1 / 90-10 participation rule**? | Reddit-wide convention many subs enforce: you should be a participant, not a drive-by. If Kirill has never posted in a sub, that sub goes lower down the list or gets skipped. |
| 5 | Are **direct download links** allowed, or must it be a text post? | Some subs auto-remove link posts from low-history accounts. |

**Default posture if a rule is ambiguous: message the mods first.** One short modmail — "I made a free open-source synth, is a release post OK here or should it go in the weekly thread?" — costs a day and eliminates the risk. For a quiet launch that is a good trade.

### Per-sub notes (judgement, not quoted rules)

- **r/FL_Studio** — the producer's home turf and the most natural fit; FL is the primary tested host. Casual, practical voice. Most likely to just work.
- **r/edmproduction** — large and production-focused; historically strict about self-promo and very likely to have a dedicated thread. **Check rule 1 hardest here.**
- **r/vst** — small and specifically about plugins, so a release post is on-topic by definition. Low reach, low risk.
- **r/synthesizers** — **skew is hardware.** A software synth may be tolerated or may be off-topic outright. My recommendation: **lowest priority, post last if at all**, and only after reading the sidebar. If in doubt, skip it — a removed post in a hardware sub costs more goodwill than it earns.

### Hacker News / r/programming

- **Show HN** is the right shape for the engineering story: it is for something you made that people can try. Title convention is `Show HN: <thing> – <short plain description>`. Do not ask for upvotes anywhere; that is the one norm HN enforces socially and mechanically.
- **r/programming** heavily favours a written post over a bare repo link. If we go there, link the engineering write-up, not the release.
- **Both are a different audience from the music subs.** Musicians do not care who wrote the code; programmers do not care about pulse width. Post on a different day and do not cross-post the same text. This is in the launch-day plan.

---

## Recommended order (quiet launch)

1. **r/FL_Studio** — home turf, best fit, most likely to be welcome.
2. **r/vst** — on-topic by definition, no drama.
3. **Bedroom Producers Blog** — pitch by email/form the same day; they run on their own schedule.
4. **KVR** — after the developer account exists.
5. **r/edmproduction** — only into the correct thread, after checking rule 1.
6. **YouTube channels** — a slow trickle, not a blast. See `YOUTUBE_CHANNELS.md`.
7. **Show HN** — a *different day*, different audience, engineering angle only.
8. **r/synthesizers** — optional, last, only if the sidebar welcomes software.
