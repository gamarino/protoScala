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
    'val kept = 7' \
    'throw new IllegalStateException("at the prompt")' \
    'kept + 1' \
    'try throw new RuntimeException("x") catch case e: RuntimeException => "recovered"' \
    '40 + 2' | timeout 90s "$P" 2>"$work/err")
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; cat "$work/err"; exit 1; }
grep -q "Not found: nope" "$work/err" || { echo "FAIL: no compile error"; cat "$work/err"; exit 1; }
grep -q "ArithmeticException: / by zero" "$work/err" || { echo "FAIL: no runtime error"; cat "$work/err"; exit 1; }
grep -q "StackOverflowError" "$work/err" || { echo "FAIL: no StackOverflowError"; cat "$work/err"; exit 1; }
grep -qF "val res" <<<"$out" && grep -qF "= 42" <<<"$out" \
    || { echo "FAIL: the session did not continue"; echo "$out"; exit 1; }
# Phase 4: a throw at the prompt is reported and the session keeps its bindings,
# which is the check that an exception does not leave the globals half-written.
grep -q "IllegalStateException: at the prompt" "$work/err" \
    || { echo "FAIL: no uncaught-exception report"; cat "$work/err"; exit 1; }
grep -qF "= 8" <<<"$out" \
    || { echo "FAIL: a binding was lost after a throw"; echo "$out"; exit 1; }
grep -qF "recovered" <<<"$out" \
    || { echo "FAIL: try/catch at the prompt"; echo "$out"; exit 1; }
echo OK
