#!/usr/bin/env bash
#
# CLI check: `protoscala --help` prints its usage text, exits 0 and shows no
# internal development labels (phase names, milestone tags).
#
# Usage: help.sh <path-to-protoscala>
set -u
PROTOSCALA="${1:?usage: help.sh <protoscala>}"

out=$("$PROTOSCALA" --help 2>&1)
rc=$?
if [[ $rc -ne 0 ]]; then
    echo "FAIL: --help exited $rc"
    exit 1
fi
if ! grep -q "Usage:" <<<"$out"; then
    echo "FAIL: --help output has no 'Usage:' line"
    exit 1
fi
labels='Phase [0-9]|v0\.[0-9]+\.x|next milestone'
if grep -qE "$labels" <<<"$out"; then
    echo "FAIL: --help shows internal milestone labels:"
    grep -nE "$labels" <<<"$out"
    exit 1
fi
echo OK
