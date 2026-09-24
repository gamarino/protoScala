#!/usr/bin/env bash
#
# CLI check: the packaged `.tar.gz` extracts and runs WITHOUT LD_LIBRARY_PATH.
#
# It builds nothing. A package build inside CTest would be slow and would need
# `dpkg`, so this re-runs the smoke test on demand: when build_pkg/ holds no
# archive it SKIPS with exit 0 and says so, which is how a reader tells a skip
# from a pass.
#
# Usage: package.sh <path-to-protoscala> <tests-source-dir>
set -u
P="${1:?usage: package.sh <protoscala> <tests-source-dir>}"
TESTS_DIR="${2:?usage: package.sh <protoscala> <tests-source-dir>}"
ROOT=$(cd "$TESTS_DIR/.." && pwd)

shopt -s nullglob
archives=("$ROOT"/build_pkg/protoscala-*-Linux.tar.gz)
shopt -u nullglob
if [[ ${#archives[@]} -eq 0 ]]; then
    echo "SKIP: no package in build_pkg/ (run cpack -G TGZ there to exercise this check)"
    exit 0
fi
archive="${archives[-1]}"

work=$(mktemp -d -p "$PWD" cli-package.XXXXXX)
trap 'rm -rf "$work"' EXIT
tar -xzf "$archive" -C "$work" --strip-components=1 || { echo "FAIL: cannot extract $archive"; exit 1; }

[[ -x "$work/bin/protoscala" ]] || { echo "FAIL: no bin/protoscala in $archive"; exit 1; }
[[ -d "$work/lib/protoscala/providers" ]] || {
    echo "FAIL: the package has no lib/protoscala/providers directory"; exit 1; }

# The packaged binary must find libprotoCore through its own RUNPATH. protoCore
# is not in the archive (it is a package dependency), so it is copied in beside
# the binary exactly as an installed system would have it.
core=$(ldd "$P" 2>/dev/null | awk '/libprotoCore/ { print $3 }')
[[ -n "$core" ]] || { echo "FAIL: cannot locate libprotoCore for the smoke test"; exit 1; }
cp -a "$(dirname "$core")"/libprotoCore.so* "$work/lib/" || {
    echo "FAIL: cannot stage libprotoCore"; exit 1; }

out=$(env -u LD_LIBRARY_PATH "$work/bin/protoscala" "$ROOT/examples/hello.scala" 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: exit $rc"; echo "$out"; exit 1; }
grep -qF 'Hello, protoScala!' <<<"$out" || { echo "FAIL: wrong output:"; echo "$out"; exit 1; }

# And it resolves libprotoCore from the package, not from a system copy: a stale
# /usr/local/lib/libprotoCore.so would otherwise silently win.
resolved=$(env -u LD_LIBRARY_PATH ldd "$work/bin/protoscala" | awk '/libprotoCore/ { print $3 }')
case "$resolved" in
    "$work"/*) ;;
    *) echo "FAIL: libprotoCore resolved to '$resolved', outside the package"; exit 1 ;;
esac
echo OK
