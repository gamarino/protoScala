#!/usr/bin/env bash
#
# CLI check: incomplete inputs continue on the next line (indentation and
# braces), and an empty continuation line forces evaluation.
#
# Usage: repl-multiline.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-multiline.sh <protoscala>}"
out=$(printf '%s\n' \
    'def twice(n: Int): Int =' \
    '  n * 2' \
    'twice(21)' \
    'val y = {' \
    '  val a = 1' \
    '  a + 1' \
    '}' \
    'val broken = (1 +' \
    '' \
    'y + 1' | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "     | " "def twice" "val res0 = 42" "val y = 2" "<console>:" "val res1 = 3"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
