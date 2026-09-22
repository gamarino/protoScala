#!/usr/bin/env bash
#
# CLI check: compile errors, runtime errors and StackOverflowError are
# reported on stderr and the session continues (protoClojure
# tests/cli/repl-stack-overflow.sh is the model).
#
# Usage: repl-errors.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-errors.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-errors.XXXXXX)
trap 'rm -rf "$work"' EXIT
out=$(printf '%s\n' \
    'println(nope)' \
    '1 / 0' \
    'def forever(n: Int): Int = forever(n + 1) + 1' \
    'forever(0)' \
    '40 + 2' | timeout 90s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; cat "$work/err"; exit 1; }
grep -q "Not found: nope" "$work/err" || { echo "FAIL: no compile error"; cat "$work/err"; exit 1; }
grep -q "ArithmeticException: / by zero" "$work/err" || { echo "FAIL: no runtime error"; cat "$work/err"; exit 1; }
grep -q "StackOverflowError" "$work/err" || { echo "FAIL: no StackOverflowError"; cat "$work/err"; exit 1; }
grep -qF "val res" <<<"$out" && grep -qF "= 42" <<<"$out" \
    || { echo "FAIL: the session did not continue"; echo "$out"; exit 1; }
echo OK
