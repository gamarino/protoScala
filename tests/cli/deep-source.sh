#!/usr/bin/env bash
#
# CLI check: source nested deeper than the native stack (60 000 nested
# braces or parentheses, a 200 000-term operator chain, a long selection
# chain ending in a syntax error) is reported as a StackOverflowError, in
# linear time, instead of crashing the process.
#
# Usage: deep-source.sh <path-to-protoscala>
set -u
P="${1:?usage: deep-source.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" deep-source.XXXXXX)
trap 'rm -rf "$work"' EXIT
repeat() { yes "$1" | head -n "$2" | tr -d '\n'; }  # $1 must not start with '-'
{ printf 'val x = '; repeat '{' 60000; printf '1'; repeat '}' 60000; printf '\nprintln(x)\n'; } \
    >"$work/braces.scala"
{ printf 'println('; repeat '(' 100000; printf '1'; repeat ')' 100000; printf ')\n'; } \
    >"$work/parens.scala"
{ printf 'println(1'; repeat '+1' 200000; printf ')\n'; } >"$work/chain.scala"
{ printf 'println(1'; repeat '+1' 200000; printf ' + nope)\n'; } >"$work/chain-error.scala"
{ printf 'val s = "a"'; repeat '.trim' 200000; printf ' +\n'; } >"$work/select-error.scala"
for f in braces parens chain chain-error select-error; do
    out=$(timeout 60s "$P" "$work/$f.scala" 2>&1); rc=$?
    case "$rc" in
        0) [[ "$f" == parens && "$out" == "1" || "$f" == chain && "$out" == "200001" ]] \
               || { echo "FAIL ($f): exit 0 with '$out'"; exit 1; } ;;
        1) grep -qE "StackOverflowError: source nested too deeply|Not found: nope|error:" <<<"$out" \
               || { echo "FAIL ($f): exit 1 without an error message: ${out:0:300}"; exit 1; } ;;
        *) echo "FAIL ($f): exit $rc (crash or timeout): ${out:0:300}"; exit 1 ;;
    esac
done
out=$(timeout 60s "$P" "$work/braces.scala" 2>&1)
grep -qF "StackOverflowError: source nested too deeply" <<<"$out" \
    || { echo "FAIL (braces): ${out:0:300}"; exit 1; }
out=$(timeout 60s "$P" --disassemble "$work/braces.scala" 2>&1); rc=$?
[[ $rc -eq 1 ]] && grep -qF "StackOverflowError" <<<"$out" \
    || { echo "FAIL (disassemble): exit $rc ${out:0:300}"; exit 1; }
echo OK
