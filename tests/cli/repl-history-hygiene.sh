#!/usr/bin/env bash
#
# CLI check: a REPL whose stdin is not a terminal never reads or writes the
# history file (tests must not touch $HOME).
#
# Usage: repl-history-hygiene.sh <path-to-protoscala>
set -u
P="${1:?usage: repl-history-hygiene.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" repl-history.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"
printf '1 + 1\n:quit\n' | HOME="$work/home" timeout 60s "$P" >/dev/null 2>&1
if [[ -e "$work/home/.protoscala_history" ]]; then
    echo "FAIL: a non-interactive session wrote the history file"
    exit 1
fi
echo OK
