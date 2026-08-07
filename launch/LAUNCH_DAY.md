# Launch day

**Posture: quiet.** You are a person putting a free thing where people can find it. Not a campaign. Nothing here needs to be done fast, and nothing here needs to be done all at once — if you get through half of it the launch still worked.

The whole thing fits in about two hours of actual attention, spread over two days.

---

## The day before — pre-flight (20 min)

Everything here is a *verify*, not a *do*. If any of it fails, fix it before a single link goes out; a broken link on launch day is the one mistake that cannot be walked back.

- [ ] **Download both zips from the release page yourself**, on a machine that has never had BLOCKWAVE on it if you can. Unzip them. Confirm they are not empty and not corrupted.
- [ ] **Install from the zip on your Mac following your own instructions**, quarantine command and all. This is the single most likely support issue and you should have done it the way a stranger will.
- [ ] **Open the landing page on your phone.** Half of Reddit is mobile. Check the GIF plays, the download buttons work, and nothing runs off the side.
- [ ] **Click both download buttons on the live site** and confirm they start a real download.
- [ ] **Check the Discovery Board is live** and the template post is pinned.
- [ ] Skim `RESEARCH_communities.md` and **run the five-point sidebar pre-flight for r/FL_Studio** — the first place you will post.
- [ ] Decide the day. **Tuesday–Thursday morning** in US/EU hours is the ordinary advice and it is fine. Avoid Friday evening and weekends; things posted then vanish.

---

## Day 1 — the music audience

Order matters here for exactly one reason: **everything you link to must already be live and correct before anyone clicks it.**

| When | Do | Why this order |
|---|---|---|
| **T+0** | Confirm the site, the release and the board are all up. | Nothing goes out before the destinations work. |
| **T+15 min** | **r/FL_Studio** — `texts/reddit-flstudio.md` | Home turf, best fit, most forgiving. If something about the post is wrong, you would rather find out here. |
| **T+1 h** | Read the first replies. Answer install questions immediately. | The first hour sets the tone. A quick honest answer to "it won't load" is worth more than any other thing you do today. |
| **T+2 h** | **r/vst** — `texts/reddit-vst.md` | On-topic by definition, low risk, different crowd. |
| **T+3 h** | **Bedroom Producers Blog** — contact form, Content tip category. `texts/bedroom-producers-blog.md` | They run on their own schedule; sending early just means it is in the queue. |
| **Same day, whenever** | **r/edmproduction** — but **only into the correct thread** if one exists. `texts/reddit-edmproduction.md` | Check rule 1 first. If there is a weekly thread, this is a two-minute job, not a post. |
| **Same day** | Start the **YouTube emails — three of them, not fifteen.** `YOUTUBE_CHANNELS.md` | Three a day for a week beats fifteen in an hour, and you will write better after seeing which questions come back. |

**Do not do on day 1:** KVR (needs the developer account first — do that in a quiet moment, it is not time-sensitive), r/synthesizers (optional, lowest priority), and anything on Hacker News.

---

## Day 2 or later — the engineering audience

**Different day. Different people. Do not cross-post.**

- [ ] Get the KVR developer account and submit the listing — `texts/kvr-listing.md`.
- [ ] **Show HN** — `texts/hackernews-or-programming.md`. Morning US time. Post it and then go and do something else; refreshing is a way to feel bad.
- [ ] Optional, only if the sidebar welcomes software: **r/synthesizers** — `texts/reddit-synthesizers.md`.

---

## The first 24 hours

Your only real job is **answering install problems.** Unsigned builds are the number one reason a free plugin gets called broken, and the fix is one command that the person did not see. Have this ready to paste:

> The builds aren't code-signed, so macOS quarantines them and your DAW will silently refuse to load the plugin. Run this and rescan:
> `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/BLOCKWAVE.vst3`
> For the standalone app, right-click it and choose Open the first time. On Windows, right-click the .vst3 folder → Properties → Unblock before copying it in.

Beyond that: answer questions, thank people, and take bug reports seriously — someone taking the time to tell you it broke is doing you a favour.

---

## What not to do

- **Do not ask for upvotes.** Anywhere. HN in particular treats this as the cardinal sin, and it is the fastest way to have a post killed.
- **Do not post the same text in two places.** Every file in `texts/` is written for its destination. Reddit users notice, and it reads as spam because it is.
- **Do not put the engineering story in front of musicians.** They do not care who wrote the code. It makes the post about you instead of about the instrument.
- **Do not argue with anyone who dislikes it.** "Fair enough, thanks for trying it" ends the exchange with your reputation intact. Nobody has ever been argued into liking a plugin.
- **Do not name a recipe.** Three are published; the other thirteen are the entire reason anyone will talk about this next month. If someone finds one and posts it, that is *good* — that is the mechanism working. You just do not spend them yourself.
- **Do not chase numbers.** Downloads on day one tell you nothing. The interesting signal arrives in two weeks, when you find out whether anyone is still using it.
- **Do not promise features to strangers.** "Maybe, I'll write it down" is a complete answer.

---

## What success looks like

Low bar, honestly stated: **a handful of people install it, one of them finds a recipe you did not tell them about, and one bug report arrives that is worth fixing.** That is a successful quiet launch. Anything above that is weather.
