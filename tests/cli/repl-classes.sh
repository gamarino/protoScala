#!/usr/bin/env bash
#
# CLI check: classes, objects, case classes, match and for at the REPL,
# multi-line template input, and class redefinition (Phase 2 plan, Task 14).
#
# Usage: repl-classes.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-classes.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-classes.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
out=$(printf '%s\n' \
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
    'object Counter:' \
    '  var n = 0' \
    '' \
    'Counter.n += 1' \
    'Counter.n' \
    'p match' \
    '  case Point(a, b) => a + b' \
    '' \
    'val (u, v) = (7, 8)' \
    'for (x <- List(1, 2)) yield x * 10' \
    'case class Point(x: Int)' \
    'p' \
    'Point(7)' \
    ':quit' | HOME="$work/home" timeout 60s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; cat "$work/err"; exit 1; }
plain="${out//scala> /}"
plain="${plain//     | /}"
for piece in '// defined case class Point' 'val p = Point(1,2)' 'val res0 = Point(1,5)' \
              '// defined trait Shape' '// defined class Sq' 'val res1 = 9.0' \
              '// defined object Counter' 'val res2 = 1' 'val res3 = 3' 'val u = 7' 'val v = 8' \
              'val res4 = List(10, 20)' 'val res5 = Point(1,2)' 'val res6 = Point(7)'; do
    grep -qxF -- "$piece" <<<"$plain" || { echo "FAIL: no '$piece' in:"; echo "$out"; cat "$work/err"; exit 1; }
done
[[ $(grep -c '// defined case class Point' <<<"$plain") -eq 2 ]] || { echo "FAIL: redefinition not echoed"; echo "$out"; exit 1; }
if grep -qF '<t' <<<"$plain"; then echo "FAIL: a generated name was echoed"; echo "$out"; exit 1; fi
if grep -qF 'defined object Point' <<<"$plain"; then echo "FAIL: synthetic companion echoed"; exit 1; fi
[[ ! -s "$work/err" ]] || { echo "FAIL: unexpected stderr:"; cat "$work/err"; exit 1; }
echo OK
