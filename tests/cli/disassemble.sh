#!/usr/bin/env bash
#
# CLI check: --disassemble prints the bytecode of a script and exits 0.
#
# Usage: disassemble.sh <path-to-protoscala> <tests-source-dir>
set -u
P="${1:?usage: disassemble.sh <protoscala> <tests-dir>}"
T="${2:?usage: disassemble.sh <protoscala> <tests-dir>}"
out=$("$P" --disassemble "$T/conformance/00-binary/hello-world.scala" 2>&1); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exited $rc: $out"; exit 1; }
for piece in "function <top>" "MAKE_FN" "function hello" "CALL 1" "RETURN"; do
    grep -qF -- "$piece" <<<"$out" || { echo "FAIL: no '$piece' in:"; echo "$out"; exit 1; }
done
echo OK
