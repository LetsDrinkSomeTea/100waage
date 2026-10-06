#!/usr/bin/env bash
# Firmware im Docker-Container bauen (feste Versionen aus waage/sketch.yaml).
# Ergebnis: build/waage.ino.bin
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
# Image-Tag aus Dockerfile + sketch.yaml: neue Versionen erzeugen ein neues Image
TAG="$(cat "$ROOT/Dockerfile" "$ROOT/waage/sketch.yaml" | sha256sum | cut -c1-12)"
IMAGE="100waage-builder:$TAG"

if ! docker image inspect "$IMAGE" &>/dev/null; then
  echo ">>> Baue Docker-Image $IMAGE (erster Lauf dauert einige Minuten)..."
  docker build -t "$IMAGE" "$ROOT"
fi

"$ROOT/tools/gen_version.sh"

echo ">>> Kompiliere Sketch..."
docker run --rm -v "$ROOT:/repo" -w /repo "$IMAGE" \
  arduino-cli compile --profile c3 --warnings default --output-dir build waage
echo ">>> Firmware: build/waage.ino.bin"
