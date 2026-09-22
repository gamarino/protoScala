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
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$T/conformance/06-recursion/fib-indent.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "6765" ]] || { echo "FAIL (fib): exit $rc, '$out'"; exit 1; }
echo OK
