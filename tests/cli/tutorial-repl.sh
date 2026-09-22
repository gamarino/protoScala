#!/usr/bin/env bash
#
# CLI check: the REPL session printed in docs/tutorial/14-repl-and-tooling.md
# §14.2 produces exactly the echoes the chapter shows.
#
# Usage: tutorial-repl.sh <path-to-protoscala>
set -u
P="${1:?usage: tutorial-repl.sh <protoscala>}"
out=$(printf '%s\n' \
    'val greeting = "Hello"' \
    'def shout(s: String) = s.toUpperCase + "!"' \
    'shout(greeting)' \
    'def fact(n: Int): BigInt = if n <= 1 then 1 else n * fact(n - 1)' \
    'fact(20)' \
    'def sumTo(n: Int): Int = {' \
    '  var total = 0' \
    '  var i = 1' \
    '  while i <= n do { total += i; i += 1 }' \
    '  total' \
    '}' \
    'sumTo(100)' \
    ':quit' | timeout 60s "$P" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
for piece in "val greeting = Hello" "def shout" "val res0 = HELLO!" "def fact" \
             "val res1 = 2432902008176640000" "def sumTo" "val res2 = 5050"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
