# RELEASE — how to cut a BLOCKWAVE release

Mechanical, repeatable steps. Follow them in order. Nothing here is creative work; if a
step needs a judgement call it says so explicitly.

Audience: Kirill (producer) and whoever is driving the build.

---

## 0. Versioning scheme

The version lives in exactly one place: **`CMakeLists.txt`**, at the top.

```cmake
project(BLOCKWAVE VERSION 1.0.0 LANGUAGES C CXX)
set(BLOCKWAVE_VERSION_TAG "rc1")
```

Two values, because they serve different masters:

| Value | Example | Who reads it |
|---|---|---|
| `PROJECT_VERSION` (numeric only) | `1.0.0` | JUCE → the VST3 / AU / `.app` bundle version, `JucePlugin_VersionString`, `JucePlugin_VersionCode`. **Must be numeric** — CMake rejects `1.0.0-rc1` outright, and JUCE turns the numbers into a packed hex version code. |
| `BLOCKWAVE_VERSION_TAG` | `rc1`, or `""` | Pre-release marker. Combined into `BLOCKWAVE_VERSION_STRING` (`1.0.0-rc1`). |

`BLOCKWAVE_VERSION_STRING` is what humans see: it names the release zips, it is printed
at configure time, and it is available to the C++ code as the `BLOCKWAVE_VERSION_STRING`
compile definition. CMake also writes it to `<build-dir>/blockwave-version.txt`, which
is what both packaging scripts read — so a zip can never be named for a version other
than the one inside it.

**To bump a version, edit those two lines and nothing else.**

- Release candidate: `VERSION 1.0.0` + `TAG "rc1"` → `1.0.0-rc1`
- Next candidate: `TAG "rc2"` → `1.0.0-rc2`
- Final: `TAG ""` → `1.0.0`
- Patch release: `VERSION 1.0.1` + `TAG ""` → `1.0.1`

Note that every release candidate for 1.0.0 reports the same *numeric* bundle version
(1.0.0) to the host. Hosts key their plug-in cache off that number, so testers moving
between rc builds should force a plug-in rescan.

---

## 1. Pre-flight (before touching the version)

- [ ] `main` is green in CI (macOS + Windows, pluginval strictness 10 both).
- [ ] Working tree is clean: `git status` shows nothing.
- [ ] qa-runner has signed off on the current checkpoint.
- [ ] The FL Studio manual checklist has passed on Kirill's Mac.
- [ ] `CHECKPOINTS/` contains the phase document for the phase being released.
- [ ] `README.md` and `docs/MANUAL.md` still describe what actually ships.
- [ ] `THIRDPARTY.md` is current.
- [ ] The manual still contains **no recipes**. Re-read it if anything about crafting
      changed.

---

## 2. Bump and tag

```bash
# 1. edit the two version lines in CMakeLists.txt
$EDITOR CMakeLists.txt

# 2. confirm CMake agrees
cmake --preset release 2>&1 | grep "BLOCKWAVE version"
#    -- BLOCKWAVE version: 1.0.0-rc1 (numeric 1.0.0)

# 3. commit
git add CMakeLists.txt
git commit -m "Release 1.0.0-rc1"

# 4. tag — annotated, name matches BLOCKWAVE_VERSION_STRING with a v prefix
git tag -a v1.0.0-rc1 -m "BLOCKWAVE 1.0.0-rc1"

# 5. push both
git push origin main
git push origin v1.0.0-rc1
```

Tag naming: `v` + `BLOCKWAVE_VERSION_STRING`. So `v1.0.0-rc1`, then `v1.0.0`.

---

## 3. Windows zip — built by CI

Pushing the tag starts the normal CI run **plus** two extra steps in the Windows job:
`Build release zip (Windows)` and `Upload release zip (Windows)`. They are gated on
`startsWith(github.ref, 'refs/tags/') || github.event_name == 'workflow_dispatch'`, so
ordinary pushes and pull requests are unaffected.

1. Watch the run: <https://github.com/nevercsof/blockwave/actions>
2. Both matrix legs must be green. pluginval s10 failing on either platform stops the
   release — no exceptions.
3. Download the **`blockwave-release-Windows`** artifact from the run summary.
4. GitHub wraps every artifact in its own zip, so you get
   `blockwave-release-Windows.zip` containing `BLOCKWAVE-<version>-Windows.zip`.
   **Unwrap it once.** The inner zip is the file you publish; do not upload the outer
   one.

The inner zip contains `BLOCKWAVE.vst3/` (the folder bundle), `LICENSE.txt`, and
`INSTALL.txt` with CRLF line endings and the `C:\Program Files\Common Files\VST3`
install path.

You can also produce it without tagging: **Actions → CI → Run workflow**
(`workflow_dispatch`) on any branch.

---

## 4. macOS zip — built locally

macOS is packaged on Kirill's Mac, not in CI, because the release carries the AU and
the Standalone and both need a human smoke test before they ship.

```bash
scripts/build.sh --clean          # full Release build: VST3 + AU + Standalone
scripts/validate.sh               # pluginval s10 on the VST3 and the AU
scripts/package-macos.sh          # -> dist/BLOCKWAVE-<version>-macOS.zip
```

The script prints the zip path, its size and its top-level contents. Expect roughly
10 MB and exactly five entries: `BLOCKWAVE.vst3`, `BLOCKWAVE.component`,
`BLOCKWAVE.app`, `LICENSE.txt`, `INSTALL.txt`.

### Signing

BLOCKWAVE ships **unsigned and un-notarized** — code signing is deferred per
`docs/SPEC.md`, and `INSTALL.txt` walks the user through Gatekeeper instead.

JUCE ad-hoc signs the bundles during the build and then writes
`Contents/Resources/moduleinfo.json` into the VST3 afterwards, which breaks the bundle
seal (`codesign -v` reports "a sealed resource is missing or invalid"). This does not
stop hosts loading the plug-in and it does not stop the app running, but if you want
self-consistent bundles:

```bash
scripts/package-macos.sh --adhoc-resign
```

That re-applies the same ad-hoc signature JUCE already uses (`codesign -s -`). It is
**not** Developer ID and **not** notarization; it changes nothing about the Gatekeeper
instructions. Off by default so the shipped bytes are exactly what the build produced.

If real signing and notarization ever land, they go in here as a separate step, and
`INSTALL.txt` gets shorter.

---

## 5. Test before publishing

Do not skip this because CI was green. CI does not open a DAW.

**macOS (Kirill, required)**

- [ ] Extract the zip somewhere clean — a different user account or a fresh folder,
      not the build tree.
- [ ] Follow `INSTALL.txt` literally, including the `xattr` commands. If a step does
      not match reality, fix `scripts/package-macos.sh` and repackage.
- [ ] FL Studio: load the VST3, play it, automate cutoff and PW, save and reopen the
      project, run four instances, do an offline render.
- [ ] Logic (or any AU host): load the `.component`, play it, save and reopen.
- [ ] Standalone: right-click → Open works from a clean download, audio comes out.
- [ ] The version reported by the host matches the numeric version you tagged.

**Windows (a beta tester, required)**

- [ ] Extract the inner zip, follow `INSTALL.txt`, copy into
      `C:\Program Files\Common Files\VST3`.
- [ ] FL Studio: rescan, load, play, automate, save and reopen the project.
- [ ] Report SmartScreen / antivirus behaviour verbatim so `INSTALL.txt` can be
      corrected if it is wrong.

Anything that fails here is a release blocker. Fix it, bump the tag (`rc2`), start
again at step 2.

---

## 6. Publish

1. GitHub → **Releases** → **Draft a new release**.
2. Choose the existing tag (`v1.0.0-rc1`). Do not let GitHub create a new one.
3. Title: `BLOCKWAVE 1.0.0-rc1`.
4. Tick **"Set as a pre-release"** for anything with a `TAG` suffix. Untick it for
   final releases only.
5. Attach both files:
   - `BLOCKWAVE-<version>-macOS.zip` (from `dist/`)
   - `BLOCKWAVE-<version>-Windows.zip` (unwrapped from the CI artifact)
6. Release notes — keep the shape identical every time:
   - One-line summary.
   - **Install**: "read INSTALL.txt in the zip", plus the one-line macOS `xattr`
     command and the Windows VST3 path.
   - **What's new**: user-visible changes only.
   - **Known issues**.
   - "Unsigned binaries — see INSTALL.txt" as an explicit line, not a footnote.
   - Link to `docs/MANUAL.md`.
   - **Never** mention a recipe, a recipe name, or how many are left to find.
7. Publish.

---

## 7. After publishing

- [ ] Download both zips **from the Releases page** — not from your build tree — and
      install once more on a clean machine or account. This catches upload corruption
      and wrong-file mistakes, which are the two most common release bugs.
- [ ] Archive the pluginval logs from the tagged CI run under `CHECKPOINTS/logs/`.
- [ ] File anything the release surfaced as issues.
- [ ] For a final (non-pre-release) version: announce per the Phase 8 launch kit.

---

## Appendix — what is where

| Thing | Where |
|---|---|
| Version numbers | `CMakeLists.txt`, top of file |
| Version string for scripts | `<build-dir>/blockwave-version.txt` (generated) |
| macOS packaging | `scripts/package-macos.sh` |
| macOS `INSTALL.txt` text | inside `scripts/package-macos.sh` (heredoc) |
| Windows packaging | `.github/workflows/ci.yml`, step `Build release zip (Windows)` |
| Windows `INSTALL.txt` text | same step (quoted heredoc, `@VERSION@` placeholder) |
| Build | `scripts/build.sh` |
| Validation | `scripts/validate.sh` |
| Release output | `dist/` (git-ignored) |
