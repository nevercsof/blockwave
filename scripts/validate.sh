#!/usr/bin/env bash
# BLOCKWAVE — pluginval strictness-10 validation of the built VST3 (+ AU best-effort).
# Usage: scripts/validate.sh [output-log-dir]   (default: CHECKPOINTS/logs)
# Copyright (C) 2026 Kirill Boyko — GPLv3 (see LICENSE).
set -euo pipefail
cd "$(dirname "$0")/.."

LOG_DIR="${1:-CHECKPOINTS/logs}"
mkdir -p "$LOG_DIR"

if command -v pluginval >/dev/null 2>&1; then
    PLUGINVAL="pluginval"
elif [[ -x "/Applications/pluginval.app/Contents/MacOS/pluginval" ]]; then
    PLUGINVAL="/Applications/pluginval.app/Contents/MacOS/pluginval"
else
    echo "ERROR: pluginval not found (brew install --cask pluginval)" >&2
    exit 1
fi

VST3=$(find build/release -name "*.vst3" -maxdepth 6 -type d | head -1)
if [[ -z "$VST3" ]]; then
    echo "ERROR: no VST3 found — run scripts/build.sh first" >&2
    exit 1
fi

echo "=== pluginval s10: $VST3 ==="
"$PLUGINVAL" --strictness-level 10 --verbose \
    --output-dir "$LOG_DIR" --validate "$VST3" \
    | tee "$LOG_DIR/pluginval-vst3-console.log"

# macOS resolves AUs only via the AudioComponent registry, so the .component
# must be installed under ~/Library/Audio/Plug-Ins/Components before pluginval
# can see it. We install (and leave it installed — handy for host testing).
AU=$(find build/release -name "*.component" -maxdepth 6 -type d | head -1)
if [[ -n "$AU" ]]; then
    AU_DEST="$HOME/Library/Audio/Plug-Ins/Components/$(basename "$AU")"
    echo "=== installing AU to $AU_DEST ==="
    mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
    rm -rf "$AU_DEST"
    cp -R "$AU" "$AU_DEST"
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    echo "=== pluginval s10: $AU_DEST ==="
    "$PLUGINVAL" --strictness-level 10 --verbose \
        --output-dir "$LOG_DIR" --validate "$AU_DEST" \
        | tee "$LOG_DIR/pluginval-au-console.log"
fi

echo "pluginval logs written to $LOG_DIR"
