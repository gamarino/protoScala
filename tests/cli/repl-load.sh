#!/usr/bin/env bash
#
# CLI check: :load runs a file in the session; its definitions stay visible.
#
# Usage: repl-load.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-load.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-load.XXXXXX)
trap 'rm -rf "$work"' EXIT
printf 'def square(x: Int): Int = x * x\nval loaded = "yes"\n' >"$work/lib.scala"
out=$(printf '%s\n' ":load $work/lib.scala" 'square(12)' 'loaded' ':help' ':nope' \
      | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "val res0 = 144" "val res1 = yes" ":load <file>" "unknown command: :nope"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
