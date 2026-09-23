#!/usr/bin/env bash
#
# CLI check: `protoscala --version` prints "protoScala X.Y.Z" and exits 0.
#
# Usage: version.sh <path-to-protoscala>
set -u
PROTOSCALA="${1:?usage: version.sh <protoscala>}"

out=$("$PROTOSCALA" --version 2>&1)
rc=$?
if [[ $rc -ne 0 ]]; then
    echo "FAIL: --version exited $rc"
    exit 1
fi
if ! grep -qE '^protoScala [0-9]+\.[0-9]+\.[0-9]+ \(actor mailboxes: .+\)$' <<<"$out"; then
    echo "FAIL: unexpected --version output: $out"
    exit 1
fi
echo OK
