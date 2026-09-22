#!/usr/bin/env bash
#
# CLI check: allocating loops and recursion run under a low protoCore heap
# ceiling (DESIGN §10). The program reports the work it did and the check
# verifies it (benchmarks and stress tests self-report).
#
# Usage: gc-pressure.sh <path-to-protoscala> <tests-source-dir>
set -u
P="${1:?usage: gc-pressure.sh <protoscala> <tests-dir>}"
T="${2:?usage: gc-pressure.sh <protoscala> <tests-dir>}"
work=$(mktemp -d -p "$PWD" gc-pressure.XXXXXX)
trap 'rm -rf "$work"' EXIT
cat >"$work/strings.scala" <<'EOF'
@main def run(): Unit =
  var s = ""
  var i = 0
  while i < 20000 do
    s = s + "x"
    i += 1
  var total = 0
  var k = 0
  while k < 20000 do
    total += ("n" + k).length
    k += 1
  println(s.length.toString + " " + total)
EOF
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$work/strings.scala" 2>&1); rc=$?
# "n0".."n9999": 2*10 + 3*90 + 4*900 + 5*9000 + 6*10000 = 108890
[[ $rc -eq 0 && "$out" == "20000 108890" ]] || { echo "FAIL (strings): exit $rc, '$out'"; exit 1; }
# Closures over boxed vars, lazy vals and varargs lists, all allocated per
# iteration. Per i: the three x contribute -1 each; then f() - 3i - 2 is 3
# for an even i and 1 for an odd one, so the 50000 odd i add -2 each.
cat >"$work/closures.scala" <<'SCALA'
def all(xs: Int*) = xs
def mk(n: Int): () => Int =
  var c = n
  lazy val base = n * 2
  () => { c += 1; c + base }
@main def run(): Unit =
  var total = 0
  var i = 0
  while i < 100000 do
    val f = mk(i)
    val g = mk(i + 1)
    all(i, i + 1, i + 2).foreach { x =>
      total = total + (if x % 2 == 0 then f() - f() else g() - g())
    }
    total = total + f() - 3 * i - 2
    i += 1
  println(total)
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$work/closures.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "-100000" ]] || { echo "FAIL (closures): exit $rc, '$out'"; exit 1; }
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$T/conformance/06-recursion/fib-indent.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "6765" ]] || { echo "FAIL (fib): exit $rc, '$out'"; exit 1; }
echo OK
