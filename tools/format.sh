#!/usr/bin/env bash
# Formats the C++ sources with clang-format, or checks them.
#   tools/format.sh            rewrite every .cpp, .hpp, .h and .mm under core, pipeline, cli and app
#   tools/format.sh --check    exit non-zero and list the files that differ (the gate's format step)
# Uses the .clang-format at the root and clang-format from the PATH or Homebrew's llvm keg. Before
# Phase 0 lands there are no sources; both forms say so and succeed.
set -uo pipefail
cd "$(dirname "$0")/.."

for p in /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin; do
  [ -d "$p" ] && PATH="$PATH:$p"
done
export PATH

check=0
case "${1:-}" in
  "") ;;
  --check) check=1 ;;
  *) echo "usage: tools/format.sh [--check]" >&2; exit 2 ;;
esac

files=()
for d in core pipeline cli app; do
  [ -d "$d" ] || continue
  while IFS= read -r f; do
    files+=("$f")
  done < <(find "$d" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.mm' \) -not -path '*/build/*' | LC_ALL=C sort)
done
if [ ${#files[@]} -eq 0 ]; then
  echo "format: no C++ sources yet"
  exit 0
fi
command -v clang-format >/dev/null || { echo "format: clang-format not found (brew install clang-format)" >&2; exit 1; }

if [ $check -eq 1 ]; then
  clang-format --dry-run --Werror "${files[@]}"
else
  clang-format -i "${files[@]}"
fi
