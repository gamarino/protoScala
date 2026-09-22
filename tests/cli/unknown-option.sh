#!/usr/bin/env bash
#
# CLI check: an unknown option exits 2 and names the option on stderr.
#
# Usage: unknown-option.sh <path-to-protoscala>
set -u
PROTOSCALA="${1:?usage: unknown-option.sh <protoscala>}"

err=$("$PROTOSCALA" --no-such-option 2>&1 >/dev/null)
rc=$?
if [[ $rc -ne 2 ]]; then
    echo "FAIL: unknown option exited $rc (expected 2)"
    exit 1
fi
if ! grep -q -- "--no-such-option" <<<"$err"; then
    echo "FAIL: stderr does not name the option: $err"
    exit 1
fi
echo OK
