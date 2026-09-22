#!/usr/bin/env bash
#
# CLI check: the REPL reads an indented construct line by line. It keeps
# reading while a line is indented deeper than the input's first line; the
# input ends on a blank line, at end of input, or when a line returns to the
# first line's column — a line there that continues the construct (`else`,
# `end while`, ...) is still part of it.
#
# Usage: repl-indented-input.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-indented-input.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-indented.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
out=$(printf '%s\n' \
    'var i = 0' \
    'while i < 3 do' \
    '  println("loop " + i)' \
    '  i += 1' \
    '' \
    'i' \
    'def f(x: Int) =' \
    '  val y = x' \
    '  y + 1' \
    'f(1)' \
    'if i > 2 then' \
    '  println("big")' \
    'else' \
    '  println("small")' \
    'var j = 0' \
    'while j < 2 do' \
    '  j += 1' \
    'end while' \
    'j' \
    'def g(n: Int): Int =' \
    '  if n > 10 then' \
    '    n' \
    '  else if n > 5 then' \
    '    n * 2' \
    '  else' \
    '    n * 3' \
    'g(1) + g(6) + g(11)' \
    'def e() =' \
    '  7' \
    | HOME="$work/home" timeout 20s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
plain="${out//scala> /}"
plain="${plain//     | /}"
for piece in 'loop 0' 'loop 1' 'loop 2' 'val res0 = 3' 'def f' 'val res1 = 2' 'big' \
              'val res2 = 2' 'def g' 'val res3 = 26' 'def e'; do
    grep -qxF -- "$piece" <<<"$plain" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
if grep -qF 'small' <<<"$plain" || grep -qF 'loop 3' <<<"$plain" || grep -qF 'error' <<<"$plain"; then
    echo "FAIL: unexpected output:"; echo "$out"; exit 1
fi
echo OK
