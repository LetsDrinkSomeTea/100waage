#!/usr/bin/env bash
# Host-Tests fuer die reinen Logik-Module (kein ESP32 noetig).
#   ./test/run.sh              normal
#   SANITIZE=1 ./test/run.sh   zusaetzlich mit AddressSanitizer + UBSan
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -I "$ROOT/waage" -I "$ROOT/test")
if [ "${SANITIZE:-0}" = 1 ]; then
  FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
fi

# test/<name>.cpp : zusaetzliche Quellen aus waage/ (durch Leerzeichen getrennt)
TESTS=(
  "duell_core_test:duell_core.cpp"
  "duell_sim_test:duell_core.cpp"
  "config_core_test:config_core.cpp"
  "scale_core_test:scale_core.cpp"
  "button_core_test:button_core.cpp"
  "battery_core_test:battery_core.cpp"
  "power_core_test:power_core.cpp"
  "text_core_test:text_core.cpp"
  "web_core_test:web_core.cpp"
)

for entry in "${TESTS[@]}"; do
  name="${entry%%:*}"
  files=("$ROOT/test/$name.cpp")
  for src in ${entry#*:}; do files+=("$ROOT/waage/$src"); done
  "$CXX" "${FLAGS[@]}" "${files[@]}" -o "$OUT/$name"
  "$OUT/$name"
done
