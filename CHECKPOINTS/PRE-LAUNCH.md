# PRE-LAUNCH — clean reinstall, landing review, security hardening

Date: 2026-08-07. Nothing was posted, emailed or announced. Two defects were found and fixed; one block could not be completed and is reported as incomplete rather than approximated.

---

## Block 1 — clean reinstall from the public release

### What I could not do, and why

**Screen access was denied**, so the GUI half of this block did not happen: I did not open the Standalone, did not open FL Studio, did not play a preset by hand, did not click around CRAFT, and there are no screenshots of any of that. I am not going to describe those steps as if they occurred.

**What that leaves for you (~5 minutes, and worth doing before launch):** the plug-ins are installed and validated right now, so just open FL Studio, rescan, load BLOCKWAVE, play a few notes, drag a block or two, and open `/Applications/BLOCKWAVE.app` with **right-click → Open**. That last one is the only step in the whole install that no automated check can stand in for, because the Gatekeeper dialog is the thing being tested.

### What I did do — the install path, executed literally

| Step | Result |
|---|---|
| **0. Inventory** | Found only the AU installed (`~/Library/Audio/Plug-Ins/Components/BLOCKWAVE.component`), left over from `validate.sh`. No VST3, no Standalone. |
| **1. Remove everything** | AU deleted, `AudioComponentRegistrar` killed. Verified all three locations clean. **Your data in `~/Documents/BLOCKWAVE` was not touched** — presets, favourites, discoveries and settings are all still there, which is what the uninstall section promises. |
| **2. Download from the public page** | Both zips pulled from `github.com/nevercsof/blockwave/releases/download/v1.0.0/`, **not** from the local build. macOS zip 10 006 039 bytes. **Both sha256 match the hashes now published in the release notes** — so the verification instructions we ship are correct and testable. |
| **3. Reproduce a real download** | `curl` does not set the quarantine flag but a browser does, so I set it by hand on all three items. Without this the test would have been meaningless — quarantine is the whole failure mode. |
| **4. Install per INSTALL.txt** | Copied VST3 and AU into the per-user folders and the app into `/Applications`, exactly as section 1 says. Quarantine confirmed present on the installed copies before continuing. |
| **5. Run the quarantine commands verbatim** | Both lines from section 2, copied character for character. Exit 0, no output — the file says "There is no output when it works", which is accurate and a genuinely useful line to have written. Both plug-ins verified clean afterwards; the `.app` deliberately left quarantined because section 3 routes it through right-click → Open instead. |
| **6. Does the installed copy actually work** | **`auval -v aumu Blkw Krbk` → AU VALIDATION SUCCEEDED** on the copy installed from the release zip: macOS registered it, it instantiates, renders and handles MIDI. **pluginval strictness 10 → SUCCESS** on the installed VST3. And a preset renders audibly: FROSTBYTE at C4 gives peak 0.439, RMS 0.097 over 384 000 samples. |

### Where the instructions were unclear to a first-time reader

Four findings. None is fatal, all are cheap to fix, and the first two are the kind of thing that turns into "it doesn't work" posts.

1. **The `.app` quarantine is never cleared, and nothing says why.** Sections 1–2 clear the flag on the plug-ins by command; section 3 then tells you to right-click → Open for the app. A reader who has just pasted two `xattr` lines will reasonably assume the app needs the same treatment and be confused when it is not listed. One sentence — *"the app is handled differently: Gatekeeper can show a dialog for an app, so you don't need a command for it"* — would close the gap.
2. **Nothing tells you what success looks like.** After the copy and the commands, there is no "you should now see BLOCKWAVE in your DAW's instrument list as *BLOCKWAVE* by *Kirill Boyko*". Without a stated expected outcome, someone whose DAW needs a restart cannot tell a slow rescan from a failed install.
3. **The rescan list omits Studio One and Bitwig**, both of which appear in the README's own host table. Small inconsistency between two files a user may read in either order.
4. **Section 1 is Finder-only.** It says "Open Finder, press Shift-Command-G and drag". Anyone comfortable with a terminal would rather have the two `cp -R` lines, and they are the same two paths already written two sections later. Worth adding as an alternative, not a replacement.

**Not a documentation problem, but worth knowing:** `auval -a` (the "list every Audio Unit" form) hangs for minutes on this machine. If you ever debug an AU, use the targeted `auval -v aumu Blkw Krbk` instead.

---

## Block 2 — landing page

### Two real defects found by looking at it

The landing page was reviewed by *fetching* it earlier in the project, and that review passed. Rendering it in a browser immediately surfaced something a text fetch structurally cannot catch.

**Defect 1 — the hero visual on the LIVE page was a placeholder.** `docs/assets/craft-discovery.gif` was a 21 KB still image with **zero animation frames** that literally rendered the word "PLACEHOLDER". The landing agent created it as a stand-in and left a comment in the HTML saying "replace with the animation from launch/presskit/"; the assets agent produced the real GIF in the same phase; nobody performed the copy, and the workflow died on rate limits before verification. **Fixed** — the real animation (62 975 bytes, 28 frames) is now in place.

**Why the earlier check missed it, which matters more than the bug:** `WebFetch` converts a page to markdown and reads it. It saw `<img alt="...block placement...RECIPE DISCOVERED...">` and truthfully reported "GIF present". The markup was right and the pixels were wrong. **A text-converting fetch cannot verify an image; only rendering can.** I have carried that into how the page is checked from here.

**Defect 2 — headings were set in the system monospace**, not in anything that looked like the product. Fixed below.

### The pixel font

`Press Start 2P`, SIL Open Font License 1.1, **self-hosted**: `docs/assets/fonts/press-start-2p-latin.woff2` (4.7 KB, latin subset) with the full `OFL.txt` alongside it. Declared with a local `@font-face` — **no Google Fonts CDN, no external request of any kind**. That was not a stylistic preference: a CDN font is a third-party request on every page load that hands Google every visitor's IP and User-Agent, which would quietly break the promise the page's own stylesheet header makes.

Applied to headings, the nav wordmark and the kicker only. **Body copy stays sans-serif** — Press Start 2P has no lowercase and enormous advance widths, and a paragraph set in it is genuinely unreadable.

Three adjustments the face demanded, all of which look wrong if skipped: `font-weight: 400` (it has no bold cut, so 700 makes the browser synthesise one and smears the pixel grid), much tighter letter-spacing (the glyphs are already widely advanced), and roughly 0.6× the previous font sizes with generous line-height (the glyphs are square and collide at normal leading).

### Rendering verified

Desktop 1280×900 and mobile 375×812, rendered over a local HTTP server — `file://` URLs come back from the tool as cached static snapshots and showed stale content, which is worth knowing for next time.

- **Desktop:** heading on one line, real animated GIF, body readable.
- **Mobile:** no horizontal scroll, heading wraps cleanly over two lines, GIF scales down, install instructions readable.
- **Zero external requests** confirmed by grep over the HTML and CSS: no CDN, no analytics, no third-party anything.
- Both download links, all three published recipes, and the unsigned-install instructions all present, with the instructions **visible without any interaction** — not behind a `<details>`.

### Email form

Uncommented as instructed. Because the endpoint still says `YOURUSERNAME`, a submission would POST to a 404 and lose the address silently, so the markup is live but wrapped in a **disabled `fieldset`**: the section renders, the note reads "Not open for signups yet — use Watch → Releases in the meantime", and the controls cannot be used. **Send me the Buttondown username and it is two edits** — the action URL and deleting one `disabled` attribute.

---

## Block 3 — security

| Item | Before | Now |
|---|---|---|
| Secrets in git history | unknown | **Clean.** `gitleaks` over all 23 commits (`--log-opts=--all`), not just the working tree: **no leaks found**. |
| GitHub secret scanning | — | **Already enabled** |
| Push protection | — | **Already enabled** |
| `GITHUB_TOKEN` permissions | none declared → **write across the whole repo** | **`permissions: contents: read`** at workflow level |
| JUCE dependency pin | `GIT_TAG 8.0.8` — **a mutable tag** | **commit `d6181bde38d858c283c3b7bf699ce6340c050b5d`** |
| Branch protection on `main` | none | **force-push blocked, deletion blocked**; both CI jobs listed as required |
| Release hashes | in a checkpoint only | **published in the release notes** |
| 2FA | **cannot determine — see below** | your action |

**Why the token permissions mattered.** Without a `permissions:` block, GitHub grants `GITHUB_TOKEN` write access across the repository. Our workflow only ever reads code — artifact upload uses its own channel — so any compromised step, ours or a third-party action's, had standing authority to push commits, move tags or edit releases. Now it has read and nothing else.

**Why the JUCE pin mattered more.** A git tag is mutable: whoever controls the upstream repository can move `8.0.8` to point at different code, and every clean build here and in CI would silently fetch it. That is the classic supply-chain shape, and for an audio plugin the payload would ship straight into other people's DAWs. It is now pinned to an immutable commit, with `GIT_SHALLOW FALSE` because a shallow clone cannot fetch an arbitrary commit. Upgrade instructions are in the comment.

**Branch protection does not get in your way.** `enforce_admins` is off, so as the repository owner you still push straight to `main` — git prints a note that the required checks "are expected" and the push goes through, which I confirmed with the commit for this checkpoint. What is genuinely blocked, for everyone including you, is **force-pushing and deleting the branch**: the two operations that rewrite history and are how a compromised account quietly swaps code under a tag people have already downloaded. That is the protection worth having on a solo repo; requiring a pull request for your own commits would only get switched off in a week.

**The hashes are now public**, which is the point: an unsigned binary on a mirror is unverifiable unless the real hashes are somewhere a stranger can read them. They are in the release body with the `shasum` and `certutil` commands. I confirmed the end-to-end story works by downloading both zips fresh and matching them against the published values.

### 2FA — needs you, and this is the one that matters most

`gh api user` returns `null` for `two_factor_authentication`, which means the token lacks the scope to read it — **not** that 2FA is off. I cannot determine it, so I am not going to guess.

**Please check: github.com → Settings → Password and authentication.** Everything else in this section defends the code. 2FA defends the *account*, and an attacker with your account does not need to defeat any of it — they can replace the release binary directly, and users have no signature to catch it because the builds are unsigned. It is the single highest-value five minutes available to this project.

---

## Left for you

1. **Check 2FA** (above).
2. **Send the Buttondown username** when the account exists.
3. **The five-minute GUI check** — FL Studio and right-click → Open on the Standalone.
4. **Pin** the Discovery Board discussion (`⋯` → Pin discussion; the API cannot).
5. Optionally, the four INSTALL.txt wording fixes — say the word and I will make them.

## Verified state

Build clean, 4947 tests green, pluginval s10 SUCCESS on both formats, landing page live with zero external requests, thirteen unpublished recipe names still absent from every public file.
