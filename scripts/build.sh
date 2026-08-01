#!/usr/bin/env bash
# BLOCKWAVE — one-command Release build (macOS).
# Usage: scripts/build.sh [--clean]
# Copyright (C) 2026 Kirill Boyko — GPLv3 (see LICENSE).
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf build/release
fi

cmake --preset release
cmake --build --preset release --parallel

echo
echo "Artifacts:"
find build/release -name "*.vst3" -maxdepth 6 -type d
find build/release -name "*.component" -maxdepth 6 -type d
find build/release -name "*.app" -maxdepth 6 -type d | grep -v "\.vst3" || true
