#!/usr/bin/env bash
# Builds the macOS package and checks it (docs/reference/testing-tools.md, **Packaging**):
# configures and builds the release preset and runs its package target (cmake/packaging.cmake),
# then mounts the disk image read-only and checks the chess-coach.app it holds:
#   - the app binary links Qt only through @rpath, @loader_path or @executable_path and nothing
#     else but system libraries (otool -L), and has no run path outside the bundle;
#   - the app starts under the offscreen platform with a scratch --data-dir, loads the bundle's
#     offscreen plugin and is still running three seconds later, having loaded no library from
#     outside the bundle but the system's (DYLD_PRINT_LIBRARIES), so a Homebrew Qt on the
#     machine cannot hide a missing one;
#   - the bundled Stockfish answers `uci` with `uciok`, with COPYING and SOURCE.txt beside it;
#   - with --db, Contents/Resources/openings.db is that file, byte for byte; without, there is
#     none;
# then unmounts the image and prints its path and size. Exits non-zero on the first failure.
#
#   tools/package.sh [--db <file>]
#
# --db bundles the opening database at <file> (the release preset's CHESS_COACH_BUNDLE_DB), a
# relative <file> being the caller's. It is refused when it is not a readable file, before the
# configure, and when the release build's own chess-coach-cli does not read it as an opening
# database (`stats --db <file>`), before the package is made. Without --db the package holds no
# database, whatever an earlier configure cached. The release workflow passes the published
# database that data/release/bundled.txt names.
#
# macOS only. Needs what the release build needs (README.md, **Building and running**) and the
# Stockfish to bundle: CHESS_COACH_STOCKFISH, else stockfish on the PATH. Not part of the gate,
# since it takes minutes; the release workflow (.github/workflows/release.yml) runs it on every
# tag. Writes build/release/ in the tree it runs in, and a scratch directory under $TMPDIR that it
# removes; the app it starts never sees the user's data directory.
set -uo pipefail

usage() {
  echo "usage: tools/package.sh [--db <file>]" >&2
  exit 2
}
db=""
case $# in
  0) ;;
  2) [ "$1" = --db ] && [ -n "$2" ] || usage
     db="$2" ;;
  *) usage ;;
esac
[ "$(uname -s)" = Darwin ] || { echo "package: macOS only" >&2; exit 2; }

fail() {
  echo "package: FAIL: $*" >&2
  exit 1
}

# The --db file is the caller's, so its path is made absolute before the move to the root.
if [ -n "$db" ]; then
  [ -f "$db" ] && [ -r "$db" ] || fail "--db $db is not a readable file"
  db="$(cd "$(dirname "$db")" && pwd -P)/$(basename "$db")"
fi
cd "$(dirname "$0")/.."

# An empty CHESS_COACH_BUNDLE_DB clears one an earlier configure cached.
cmake --preset release -DCHESS_COACH_BUNDLE_DB="$db" >/dev/null || fail "configure"
cmake --build --preset release || fail "build"
if [ -n "$db" ]; then
  build/release/cli/chess-coach-cli stats --db "$db" >/dev/null ||
    fail "--db $db is not an opening database this build reads"
fi
cmake --build --preset release --target package || fail "the package target"

name=$(sed -n 's/^set(CPACK_PACKAGE_FILE_NAME "\(.*\)")$/\1/p' build/release/CPackConfig.cmake)
dmg="build/release/$name.dmg"
[ -n "$name" ] || fail "no CPACK_PACKAGE_FILE_NAME in build/release/CPackConfig.cmake"
[ -f "$dmg" ] || fail "no disk image at $dmg"

scratch=$(mktemp -d) || fail "mktemp"
mnt="$scratch/mnt"
app_pid=""
cleanup() {
  [ -z "$app_pid" ] || kill "$app_pid" 2>/dev/null
  hdiutil detach "$mnt" -quiet 2>/dev/null
  rm -rf "$scratch"
}
trap cleanup EXIT
mkdir -p "$mnt" "$scratch/data"
hdiutil attach -readonly -nobrowse -noautoopen -mountpoint "$mnt" "$dmg" >/dev/null ||
  fail "hdiutil attach $dmg"
app="$(cd "$mnt" && pwd -P)/chess-coach.app"
binary="$app/Contents/MacOS/chess-coach"
[ -x "$binary" ] || fail "no executable chess-coach.app/Contents/MacOS/chess-coach in the image"

# The app binary's libraries and run paths.
libraries=$(otool -L "$binary" | tail -n +2 | awk '{print $1}')
bundled_qt='^(@rpath|@loader_path|@executable_path)/(\.\./Frameworks/)?Qt[A-Za-z]+\.framework/'
foreign=$(printf '%s\n' "$libraries" | grep -Ev "$bundled_qt|^/System/Library/|^/usr/lib/")
[ -z "$foreign" ] || fail "the app binary links outside the bundle and the system: $foreign"
qt=$(printf '%s\n' "$libraries" | grep -c '\.framework/Versions/A/Qt')
rpaths=$(otool -l "$binary" | awk '/cmd LC_RPATH/ {r = 1} r && $1 == "path" {print $2; r = 0}')
foreign=$(printf '%s\n' "$rpaths" | grep -v '^@' | grep -v '^$')
[ -z "$foreign" ] || fail "the app binary has a run path outside the bundle: $foreign"
echo "package: otool -L: $(printf '%s\n' "$libraries" | wc -l | tr -d ' ') libraries," \
  "$qt Qt frameworks in the bundle, the rest /System/Library and /usr/lib"

# The app, offscreen, for three seconds once Qt has loaded its platform plugin from the bundle
# (the first start of a new image can wait seconds on the system's security scan).
DYLD_PRINT_LIBRARIES=1 QT_QPA_PLATFORM=offscreen "$binary" --data-dir "$scratch/data" \
  >"$scratch/app.out" 2>"$scratch/app.err" &
app_pid=$!
for _ in $(seq 60); do
  grep -q "^dyld\[[0-9]*\]: <[^>]*> $app/Contents/PlugIns/platforms/libqoffscreen.dylib$" \
    "$scratch/app.err" && break
  kill -0 "$app_pid" 2>/dev/null || break
  sleep 0.5
done
sleep 3
kill -0 "$app_pid" 2>/dev/null || {
  app_pid=""
  grep -v '^dyld\[' "$scratch/app.err" >&2
  fail "the bundled app exited"
}
grep -q "^dyld\[[0-9]*\]: <[^>]*> $app/Contents/PlugIns/platforms/libqoffscreen.dylib$" \
  "$scratch/app.err" || fail "the bundled app did not load the bundle's offscreen platform plugin"
kill "$app_pid" && wait "$app_pid" 2>/dev/null
app_pid=""
loaded=$(sed -nE 's/^dyld\[[0-9]+\]: <[^>]*> (.*)$/\1/p' "$scratch/app.err" | sort -u)
foreign=$(printf '%s\n' "$loaded" | grep -v "^$app/" | grep -Ev '^/System/|^/usr/lib/')
[ -z "$foreign" ] || fail "the bundled app loaded libraries from outside the bundle: $foreign"
echo "package: the app ran three seconds offscreen, $(printf '%s\n' "$loaded" |
  grep -c "^$app/") images from the bundle and none from outside it but the system's"

# The engine.
engine="$app/Contents/Resources/engine"
printf 'uci\nquit\n' | "$engine/stockfish" | grep -qx uciok ||
  fail "the bundled Stockfish did not answer uci with uciok"
[ -f "$engine/COPYING" ] && [ -f "$engine/SOURCE.txt" ] ||
  fail "no COPYING or SOURCE.txt beside the engine"
echo "package: the bundled Stockfish answered uciok; SOURCE.txt:"
sed 's/^/  /' "$engine/SOURCE.txt"

# The opening database.
bundled="$app/Contents/Resources/openings.db"
if [ -n "$db" ]; then
  cmp -s "$db" "$bundled" || fail "Contents/Resources/openings.db is not $db"
  echo "package: Contents/Resources/openings.db is $db ($(stat -f %z "$bundled") bytes)"
else
  [ ! -e "$bundled" ] || fail "Contents/Resources/openings.db is there without --db"
  echo "package: no opening database bundled"
fi

hdiutil detach "$mnt" -quiet || fail "hdiutil detach"
echo "package: $dmg, $(du -h "$dmg" | cut -f1 | tr -d ' ') ($(stat -f %z "$dmg") bytes)"
