#!/usr/bin/env bash
#
# CLI check: REPL redefinition shadows (Scala REPL semantics). Code compiled
# earlier keeps the binding it saw, even when the new definition changes the
# kind (def -> val, val -> lazy val), and an input whose initialiser throws
# defines nothing.
#
# Usage: repl-redefinition.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-redefinition.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-redefinition.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
out=$(printf '%s\n' \
    'val x = 1' 'def f = x' 'val x = 2' 'f' \
    'def g = 1' 'def h = g + 1' 'val g = 10' 'h' \
    'val k = 5' 'def useK = k' 'lazy val k = 7' 'useK' 'k' \
    'val w = 1' 'val w = 1 / 0' 'w' \
    'val z = 1 / 0' 'z' \
    'var v = 1' 'def bump() = v += 1' 'var v = 100' 'bump()' 'v' \
    | HOME="$work/home" timeout 60s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; cat "$work/err"; exit 1; }
for piece in "val res0 = 1" "val res1 = 2" "val res2 = 5" "val res3 = 7" "val res4 = 1" \
              "val res5 = 100"; do
    grep -qxF -- "$piece" <<<"${out//scala> /}" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
grep -qF "Not found: z" "$work/err" || { echo "FAIL: z was half-defined"; cat "$work/err"; exit 1; }
[[ $(grep -c "ArithmeticException" "$work/err") -eq 2 ]] \
    || { echo "FAIL: expected two ArithmeticExceptions"; cat "$work/err"; exit 1; }
echo OK
