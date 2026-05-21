#!/usr/bin/env bash
set -e

FQBN="esp32:esp32:nologo_esp32c3_super_mini"
IMAGE="100waage-builder"
SKETCH_DIR="$(cd "$(dirname "$0")/waage" && pwd)"

# Build image if not present
if ! docker image inspect "$IMAGE" &>/dev/null; then
  echo ">>> Building Docker image (first run — takes a few minutes)..."
  docker build -t "$IMAGE" "$(dirname "$0")"
fi

echo ">>> Compiling sketch..."
docker run --rm \
  -v "$SKETCH_DIR:/waage" \
  "$IMAGE" \
  arduino-cli compile --fqbn "$FQBN" /waage
