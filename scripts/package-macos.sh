#!/usr/bin/env bash
# BLOCKWAVE — build a distributable macOS zip (VST3 + AU + Standalone + docs).
#
# Usage: scripts/package-macos.sh [build-dir] [--adhoc-resign]
#          build-dir       default: build/release
#          --adhoc-resign  re-apply the AD-HOC signature JUCE already puts on the
#                          bundles. Not Developer ID, not notarization — it only
#                          repairs the bundle seal that JUCE itself breaks by
#                          writing Contents/Resources/moduleinfo.json into the
#                          VST3 *after* signing it. Off by default so the shipped
#                          bytes stay exactly what scripts/build.sh produced.
#
# The binaries are UNSIGNED and UN-NOTARIZED by design (code signing is deferred
# per docs/SPEC.md), so the bundled INSTALL.txt tells the user exactly how to get
# past Gatekeeper. Run scripts/build.sh first.
#
# Copyright (C) 2026 Kirill Boyko — GPLv3 (see LICENSE).
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="build/release"
ADHOC_RESIGN=0
for arg in "$@"; do
    case "$arg" in
        --adhoc-resign) ADHOC_RESIGN=1 ;;
        -*)             echo "ERROR: unknown option $arg" >&2; exit 2 ;;
        *)              BUILD_DIR="$arg" ;;
    esac
done
OUT_DIR="dist"

# --- version ----------------------------------------------------------------
# CMake writes build/<preset>/blockwave-version.txt at generate time, so the zip
# name can never drift from the binaries inside it. Fall back to CMakeLists.txt
# if the caller points us at a tree that predates that.
if [[ -f "$BUILD_DIR/blockwave-version.txt" ]]; then
    VERSION="$(tr -d '[:space:]' < "$BUILD_DIR/blockwave-version.txt")"
else
    NUM=$(sed -n 's/^project(BLOCKWAVE VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)
    TAG=$(sed -n 's/^set(BLOCKWAVE_VERSION_TAG "\(.*\)").*/\1/p' CMakeLists.txt | head -1)
    VERSION="$NUM${TAG:+-$TAG}"
    echo "NOTE: $BUILD_DIR/blockwave-version.txt missing; using CMakeLists.txt ($VERSION)" >&2
fi
[[ -n "$VERSION" ]] || { echo "ERROR: could not determine version" >&2; exit 1; }

PKG_NAME="BLOCKWAVE-${VERSION}-macOS"
STAGE="$OUT_DIR/$PKG_NAME"
ZIP="$OUT_DIR/$PKG_NAME.zip"

# --- locate artefacts -------------------------------------------------------
find_bundle() {  # $1 = extension, $2 = human name
    local hit
    hit=$(find "$BUILD_DIR" -maxdepth 6 -type d -name "*.$1" | grep -v '\.vst3/' | head -1)
    if [[ -z "$hit" ]]; then
        echo "ERROR: no .$1 ($2) found under $BUILD_DIR — run scripts/build.sh" >&2
        exit 1
    fi
    printf '%s\n' "$hit"
}

VST3=$(find_bundle vst3 "VST3")
AU=$(find_bundle component "Audio Unit")
APP=$(find_bundle app "Standalone")

echo "=== BLOCKWAVE $VERSION — packaging from $BUILD_DIR ==="
echo "  VST3       : $VST3"
echo "  AU         : $AU"
echo "  Standalone : $APP"

# --- stage ------------------------------------------------------------------
rm -rf "$STAGE" "$ZIP"
mkdir -p "$STAGE"
# ditto (not cp) so bundle symlinks and permissions survive verbatim.
ditto "$VST3" "$STAGE/$(basename "$VST3")"
ditto "$AU"   "$STAGE/$(basename "$AU")"
ditto "$APP"  "$STAGE/$(basename "$APP")"
cp LICENSE "$STAGE/LICENSE.txt"

# Strip Finder/provenance extended attributes so `ditto -c -k` does not emit a
# ._AppleDouble twin for every file (harmless, but it looks broken to anyone
# extracting the zip from a terminal). Bundle signatures live inside the
# binaries and Contents/_CodeSignature, never in xattrs, so nothing is lost.
xattr -cr "$STAGE"

if [[ "$ADHOC_RESIGN" == "1" ]]; then
    echo "=== ad-hoc re-signing (repairing the JUCE moduleinfo.json seal) ==="
    for b in "$STAGE/$(basename "$VST3")" "$STAGE/$(basename "$AU")" "$STAGE/$(basename "$APP")"; do
        codesign --force --deep --sign - "$b"
        codesign --verify --strict "$b" && echo "  ok: $(basename "$b")"
    done
fi

VST3_NAME=$(basename "$VST3")
AU_NAME=$(basename "$AU")
APP_NAME=$(basename "$APP")

# Column-aligned contents list — the bundle names are not a fixed width, so
# hard-coded padding in the heredoc would drift the moment one of them changes.
FILE_LIST=$(printf '  %-24s%s\n' \
    "$VST3_NAME" "the VST3 plug-in  (FL Studio, Live, Reaper, Bitwig, Cubase...)" \
    "$AU_NAME"   "the Audio Unit    (Logic Pro, GarageBand, MainStage)" \
    "$APP_NAME"  "the standalone app (no DAW needed)" \
    "LICENSE.txt" "the GNU General Public License, version 3" \
    "INSTALL.txt" "this file")

cat > "$STAGE/INSTALL.txt" <<EOF
BLOCKWAVE $VERSION — macOS install instructions
===============================================================================

BLOCKWAVE is free software (GPLv3). Every sound is a square.

WHAT IS IN THIS FOLDER
-------------------------------------------------------------------------------
$FILE_LIST

These binaries are NOT code-signed and NOT notarized by Apple. That is normal
for a free, open-source plug-in — it does not mean anything is wrong with them.
It does mean macOS will refuse to open them until you tell it to, which is what
steps 1-3 below are for. If you would rather not trust a stranger's binary, the
full source is on GitHub and builds in one command (see the README).


1. INSTALL THE PLUG-INS
-------------------------------------------------------------------------------
Open Finder. Press Shift-Command-G and paste each path, then drag the matching
item from this folder into the window that opens. Create the folder if it does
not exist yet.

  VST3  ->  ~/Library/Audio/Plug-Ins/VST3
  AU    ->  ~/Library/Audio/Plug-Ins/Components

(Those are the per-user folders and need no admin password. If you prefer to
install for every user on the Mac, use /Library/Audio/Plug-Ins/VST3 and
/Library/Audio/Plug-Ins/Components instead and adjust the paths in step 2.)

You can put $APP_NAME anywhere — /Applications is the usual home.


2. REMOVE THE QUARANTINE FLAG FROM THE PLUG-INS
-------------------------------------------------------------------------------
Everything downloaded from a browser gets tagged "quarantined". Hosts load
plug-ins without any way to show you an "Open anyway" dialog, so the tag has to
be cleared from the command line — otherwise your DAW will simply report that
BLOCKWAVE failed to load, or will not list it at all.

Open Terminal (Applications > Utilities > Terminal) and paste these two lines,
one at a time, pressing Return after each:

  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/$VST3_NAME
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/$AU_NAME

There is no output when it works. (If you installed system-wide in step 1, use
/Library/... instead of ~/Library/... and prefix each line with "sudo ".)

Then rescan your plug-ins:
  * FL Studio : Options > Manage plugins > Find more plugins
  * Live      : Preferences > Plug-Ins > Rescan
  * Logic     : it rescans on launch; quit and reopen Logic
  * Reaper    : Preferences > Plug-ins > VST > Re-scan


3. OPEN THE STANDALONE APP THE FIRST TIME
-------------------------------------------------------------------------------
Do NOT double-click $APP_NAME the first time — macOS will say it "cannot be
opened because the developer cannot be verified" and offer only Move to Trash.

Instead: RIGHT-CLICK (or Control-click) $APP_NAME, choose "Open" from the menu,
and click "Open" in the dialog that appears. You only ever do this once; after
that it launches normally by double-click.

On macOS Sequoia (15) and newer, right-click > Open may no longer show an Open
button. If so, double-click it once, let it be blocked, then go to
System Settings > Privacy & Security, scroll to the bottom, and click
"Open Anyway" next to BLOCKWAVE.

The command-line equivalent, if you prefer it:
  xattr -dr com.apple.quarantine /Applications/$APP_NAME


4. UNINSTALL
-------------------------------------------------------------------------------
Delete these; nothing else is touched:
  ~/Library/Audio/Plug-Ins/VST3/$VST3_NAME
  ~/Library/Audio/Plug-Ins/Components/$AU_NAME
  /Applications/$APP_NAME
  ~/Documents/BLOCKWAVE          (your presets, favourites, discoveries, settings)

Keep ~/Documents/BLOCKWAVE if you want to keep your own presets and your
discovered-recipe count.


REQUIREMENTS
-------------------------------------------------------------------------------
  macOS 10.15 Catalina or newer, Intel or Apple Silicon.
  A host that loads VST3 or AU instruments, or nothing at all for the
  standalone app.


LICENSE
-------------------------------------------------------------------------------
BLOCKWAVE is free software released under the GNU General Public License v3.
See LICENSE.txt. Copyright (C) 2026 Kirill Boyko.
Source, manual and issue tracker: https://github.com/nevercsof/blockwave
EOF

# --- zip --------------------------------------------------------------------
# ditto -c -k --keepParent writes a real macOS-safe archive: bundle symlinks,
# the executable bit and the ad-hoc signature survive, which a plain `zip -r`
# does not guarantee. --norsrc/--noextattr/--noqtn keep the archive clean: without
# them ditto emits a ._AppleDouble twin for every entry (or, with
# --sequesterRsrc, a __MACOSX/ folder), which looks broken to anyone extracting
# from a terminal. BLOCKWAVE's bundles carry no resource forks, so nothing real
# is dropped.
ditto -c -k --keepParent --norsrc --noextattr --noqtn "$STAGE" "$ZIP"

SIZE=$(du -h "$ZIP" | cut -f1)
echo
echo "=== $ZIP ($SIZE) ==="
echo "Top level of the archive:"
unzip -Z1 "$ZIP" | awk -F/ 'NF>1 && $2 != "" { print "  " $2 }' | sort -u
echo
echo "Staged folder kept at $STAGE for inspection."
