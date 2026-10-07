#!/usr/bin/env bash
# Formatiert C++ (clang-format) und Markdown/YAML (Prettier).
# Ohne Argumente: alle versionierten Dateien. Mit Argumenten: nur diese.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

if [ $# -gt 0 ]; then files=("$@"); else mapfile -t files < <(git ls-files); fi

cpp=() pretty=()
for f in "${files[@]}"; do
  [ -f "$f" ] || continue
  case "$f" in
    *.cpp | *.h | *.ino) cpp+=("$f") ;;
    *.md | *.yml | *.yaml) pretty+=("$f") ;;
  esac
done

if [ ${#cpp[@]} -gt 0 ]; then
  for f in "${cpp[@]}"; do
    # .ino kennt clang-format nicht als Endung.
    clang-format -i --assume-filename="${f%.ino}.cpp" "$f"
  done
fi
if [ ${#pretty[@]} -gt 0 ]; then
  prettier=$(command -v prettier || echo "$HOME/.local/share/nvim/mason/bin/prettier")
  "$prettier" --write --log-level warn "${pretty[@]}"
fi
