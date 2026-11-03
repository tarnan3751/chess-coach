#!/usr/bin/env bash
# Prints the CHANGELOG.md section for one release, generated from the Conventional Commit log
# (CLAUDE.md, **Release notes**; the commit convention is the one tools/land_series.sh checks).
#
#   tools/changelog.sh <version> [<from-ref>] [<to-ref>]
#
# The section covers the commits in <from-ref>..<to-ref>. <to-ref> defaults to HEAD.
# <from-ref> defaults to the previous tag reachable from <to-ref> (`git describe --tags
# --abbrev=0 <to-ref>^`), or to the root when there is none, in which case the whole history
# of <to-ref> is covered, the root commit included; pass `--` as <from-ref> to keep that
# default while naming a <to-ref>. The output is a heading `## <version> — <date>` (the ISO
# date of <to-ref>'s commit), then one subsection per commit type present, in the order
# Features (feat), Fixes (fix), Performance (perf), Refactoring (refactor), Build (build),
# CI (ci), Documentation (docs), Tests (test), Maintenance (chore, style), Reverts (revert),
# and last Other, for subjects that are not Conventional Commits. Each commit is a line
# `- **scope:** Summary (`sha`)`, or `- Summary (`sha`)` without a scope, oldest first; a
# subject with the `!` breaking marker gets `**BREAKING:** ` before its summary, as in
# `- **core:** **BREAKING:** Summary (`sha`)`.
#
# To add a release to CHANGELOG.md, after tagging it:
#   tools/changelog.sh 0.2.0 v0.1.0 v0.2.0
# and paste the output below the file's header, above the previous release, with a blank line
# between the two. The file is committed as generated, never edited by hand.
#
# Pure bash and git, no GNU-only flags, so it runs in Git Bash on Windows and in macOS's bash
# 3.2. It reads the repository it is run in.
set -uo pipefail

usage() {
  echo "usage: tools/changelog.sh <version> [<from-ref>|--] [<to-ref>]" >&2
  exit 2
}

[ $# -ge 1 ] && [ $# -le 3 ] && [ -n "$1" ] || usage
version="$1"
from="${2:-}"
to="${3:-HEAD}"
[ "$from" = "--" ] && from=""

git rev-parse --verify -q "$to^{commit}" >/dev/null || {
  echo "changelog: not a commit: $to" >&2
  exit 2
}
if [ -z "$from" ]; then
  from="$(git describe --tags --abbrev=0 "$to^" 2>/dev/null || true)"
fi
if [ -n "$from" ]; then
  git rev-parse --verify -q "$from^{commit}" >/dev/null || {
    echo "changelog: not a commit: $from" >&2
    exit 2
  }
  range="$from..$to"
else
  range="$to"
fi

# The subject rule of tools/land_series.sh.
conventional='^(feat|fix|refactor|perf|test|docs|build|ci|chore|style|revert)(\([a-z0-9][a-z0-9/_-]*\))?!?: [A-Z0-9].*[^.[:space:]]$'

# Each commit becomes "<type> <line>", oldest first; <type> is "other" for a subject that is
# not a Conventional Commit.
entries=()
while IFS=' ' read -r sha subject; do
  if [[ "$subject" =~ $conventional ]]; then
    type="${BASH_REMATCH[1]}"
    scope="${BASH_REMATCH[2]}"
    summary="${subject#*: }"
    case "${subject%%: *}" in
      *!) summary="**BREAKING:** $summary" ;;
    esac
    if [ -n "$scope" ]; then
      scope="${scope#(}"
      line="- **${scope%)}:** $summary (\`$sha\`)"
    else
      line="- $summary (\`$sha\`)"
    fi
  else
    type="other"
    line="- $subject (\`$sha\`)"
  fi
  entries+=("$type $line")
done < <(git log --reverse --format='%h %s' "$range")

printf '## %s — %s\n' "$version" "$(git log -1 --format=%cs "$to")"

# section <title> <type>...
section() {
  local title="$1" entry type printed=0
  shift
  for entry in ${entries[@]+"${entries[@]}"}; do
    for type in "$@"; do
      if [ "${entry%% *}" = "$type" ]; then
        [ $printed -eq 1 ] || printf '\n### %s\n\n' "$title"
        printed=1
        printf '%s\n' "${entry#* }"
      fi
    done
  done
}

section Features feat
section Fixes fix
section Performance perf
section Refactoring refactor
section Build build
section CI ci
section Documentation docs
section Tests test
section Maintenance chore style
section Reverts revert
section Other other
