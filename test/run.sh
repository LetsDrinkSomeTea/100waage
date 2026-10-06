#!/usr/bin/env bash
# Host-Tests fuer die Duell-Logik (kein ESP32 noetig).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -I "$ROOT/waage" -I "$ROOT/test")

for t in duell_core_test duell_sim_test; do
  "$CXX" "${FLAGS[@]}" "$ROOT/test/$t.cpp" "$ROOT/waage/duell_core.cpp" -o "$OUT/$t"
  "$OUT/$t"
done
