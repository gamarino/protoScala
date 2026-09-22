#!/usr/bin/env bash
#
# CLI check: the REPL evaluates definitions and expressions from piped stdin,
# echoes values, and exits 0 on :quit.
#
# Usage: repl-basic.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-basic.sh <protoscala>}"
out=$(printf '%s\n' 'val x = 40' 'def inc(n: Int) = n + 1' 'inc(x) + 1' '"a" + "b"' ':quit' \
      | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "protoScala " "val x = 40" "def inc" "val res0 = 42" "val res1 = ab"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
