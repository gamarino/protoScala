#!/usr/bin/env bash
#
# CLI check: `protoscala --run-module` (Phase 7 Task 10 Step 5).
#
# Usage: run-module.sh <protoscala> <tests-dir> <protoscalac> <scratch-dir>
set -u
PROTOSCALA="${1:?usage: run-module.sh <protoscala> <tests-dir> <protoscalac> <scratch>}"
TESTS_DIR="${2:?}"
PROTOSCALAC="${3:?}"
SCRATCH="${4:?}"

fails=0
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

# A missing file: exit 1, and the message names the path.
"$PROTOSCALA" --run-module "$SCRATCH/nope.so" >"$SCRATCH/miss.out" 2>&1
[[ $? -eq 1 ]] || { echo "FAIL: a missing .so did not exit 1"; fails=$((fails + 1)); }
grep -q 'nope.so' "$SCRATCH/miss.out" || {
    echo "FAIL: the missing .so was not named"; fails=$((fails + 1)); }

# --run-module with no path at all.
"$PROTOSCALA" --run-module >"$SCRATCH/none.out" 2>&1
[[ $? -eq 2 ]] || { echo "FAIL: --run-module with no path did not exit 2"; fails=$((fails + 1)); }

# A shared library that is not a protoScala module: the message says which symbol
# is missing, rather than a dlsym error nobody can act on.
cat > "$SCRATCH/notamodule.cpp" <<'CPP'
extern "C" int unrelated() { return 0; }
CPP
if c++ -shared -fPIC -o "$SCRATCH/notamodule.so" "$SCRATCH/notamodule.cpp" 2>/dev/null; then
    "$PROTOSCALA" --run-module "$SCRATCH/notamodule.so" >"$SCRATCH/nota.out" 2>&1
    [[ $? -eq 1 ]] || { echo "FAIL: a non-module .so did not exit 1"; fails=$((fails + 1)); }
    grep -q 'not a protoScala module: proto_module_init not found' "$SCRATCH/nota.out" || {
        echo "FAIL: the non-module message is missing:"; sed 's/^/  /' "$SCRATCH/nota.out"
        fails=$((fails + 1)); }
else
    echo "note: no C++ compiler for the non-module case; skipped"
fi

# The transpiled hello-world runs and prints what the interpreted one prints.
HELLO="$TESTS_DIR/conformance/00-binary/hello-world.scala"
if "$PROTOSCALAC" "$HELLO" -o "$SCRATCH" --build-so >"$SCRATCH/build.out" 2>&1; then
    out=$("$PROTOSCALA" --run-module "$SCRATCH/module.so" 2>&1)
    rc=$?
    [[ $rc -eq 0 ]] || { echo "FAIL: --run-module exited $rc"; fails=$((fails + 1)); }
    interpreted=$("$PROTOSCALA" "$HELLO" 2>&1)
    if [[ "$out" != "$interpreted" ]]; then
        echo "FAIL: transpiled '$out' != interpreted '$interpreted'"
        fails=$((fails + 1))
    fi
else
    echo "FAIL: --build-so failed"
    sed 's/^/  /' "$SCRATCH/build.out"
    fails=$((fails + 1))
fi

[[ $fails -eq 0 ]] || exit 1
echo OK
