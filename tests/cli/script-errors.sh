#!/usr/bin/env bash
#
# CLI check: failing scripts exit 1 with a located message on stderr, and the
# stdout printed before a runtime error is kept.
#
# Usage: script-errors.sh <path-to-protoscala>
set -u
P="${1:?usage: script-errors.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" script-errors.XXXXXX)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

"$P" "$work/missing.scala" >/dev/null 2>"$work/err"; rc=$?
[[ $rc -eq 1 ]] || fail "missing file exited $rc"
grep -q "cannot open" "$work/err" || fail "missing file message: $(cat "$work/err")"

"$P" "$work" >/dev/null 2>"$work/err"; rc=$?
[[ $rc -eq 1 ]] || fail "directory path exited $rc"
grep -q "cannot open" "$work/err" || fail "directory path message: $(cat "$work/err")"

printf 'val x = (1 +\nval y = 2\n' >"$work/parse.scala"
"$P" "$work/parse.scala" >/dev/null 2>"$work/err"; rc=$?
[[ $rc -eq 1 ]] || fail "parse error exited $rc"
grep -qE 'parse\.scala:[0-9]+:[0-9]+: error: ' "$work/err" || fail "parse error format: $(cat "$work/err")"

printf '@main def run(): Unit =\n  println("before")\n  val zero = 0\n  println(1 / zero)\n' >"$work/runtime.scala"
out=$("$P" "$work/runtime.scala" 2>"$work/err"); rc=$?
[[ $rc -eq 1 ]] || fail "runtime error exited $rc"
[[ "$out" == "before" ]] || fail "stdout before the error was lost: '$out'"
grep -q 'runtime\.scala:4: error: ArithmeticException: / by zero' "$work/err" \
    || fail "runtime error format: $(cat "$work/err")"
echo OK
