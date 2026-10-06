#!/usr/bin/env bash
# Schreibt waage/version_gen.h mit der Firmware-Version aus `git describe`.
# Die Datei wird nur neu geschrieben, wenn sich die Version aendert.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/waage/version_gen.h"
VER="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo nogit)"
NEW="#define FW_VERSION \"$VER\""

if [ ! -f "$OUT" ] || [ "$(cat "$OUT")" != "$NEW" ]; then
  echo "$NEW" > "$OUT"
fi
echo "FW_VERSION=$VER"
