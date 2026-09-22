#!/usr/bin/env bash
#
# CLI check: the shipped examples run and print what they compute.
#
# Usage: examples.sh <path-to-protoscala> <repo-root>
set -u
P="${1:?usage: examples.sh <protoscala> <repo-root>}"
R="${2:?usage: examples.sh <protoscala> <repo-root>}"
out=$(timeout 60s "$P" "$R/examples/hello.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "Hello, protoScala!" ]] || { echo "FAIL hello: exit $rc, '$out'"; exit 1; }
out=$(timeout 60s "$P" "$R/examples/fib.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "fib(25) = 75025" ]] || { echo "FAIL fib: exit $rc, '$out'"; exit 1; }
out=$(timeout 60s "$P" "$R/examples/fib.scala" 10 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "fib(10) = 55" ]] || { echo "FAIL fib 10: exit $rc, '$out'"; exit 1; }
echo OK
