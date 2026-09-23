#!/usr/bin/env bash
#
# CLI check: the REPL sessions printed in docs/tutorial/14-repl-and-tooling.md
# §14.2 and §14.8 produce exactly the echoes the chapters show.
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
for piece in 'val greeting = "Hello"' "def shout" 'val res0 = "HELLO!"' "def fact" \
             "val res1 = 2432902008176640000" "def sumTo" "val res2 = 5050"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done

# --- §14.8 "Classes at the REPL": the transcript the chapter prints ------------
# Its inputs are the first lines of tests/cli/repl-classes.sh, so the two checks
# cannot drift apart. HOME is redirected so the real ~/.protoscala_history is
# never touched.
work=$(mktemp -d -p "$PWD" tutorial-repl.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
classes_out=$(printf '%s\n' \
    'case class Point(x: Int, y: Int)' \
    'val p = Point(1, 2)' \
    'p.copy(y = 5)' \
    'trait Shape:' \
    '  def area: Double' \
    '' \
    'class Sq(s: Double) extends Shape:' \
    '  def area = s * s' \
    '' \
    'new Sq(3.0).area' \
    ':quit' | HOME="$work/home" timeout 60s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: §14.8 exit $rc"; echo "$classes_out"; cat "$work/err"; exit 1; }
plain="${classes_out//scala> /}"
plain="${plain//     | /}"
for piece in '// defined case class Point' 'val p = Point(1,2)' 'val res0 = Point(1,5)' \
             '// defined trait Shape' '// defined class Sq' 'val res1 = 9.0'; do
    grep -qxF -- "$piece" <<<"$plain" || {
        echo "FAIL: §14.8 has no '$piece' in:"; echo "$classes_out"; cat "$work/err"; exit 1; }
done
[[ ! -s "$work/err" ]] || { echo "FAIL: §14.8 unexpected stderr:"; cat "$work/err"; exit 1; }
[[ -z "$(ls -A "$work/home")" ]] || { echo "FAIL: the REPL wrote to HOME"; ls -A "$work/home"; exit 1; }
echo OK
