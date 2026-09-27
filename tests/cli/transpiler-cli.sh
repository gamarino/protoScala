#!/usr/bin/env bash
#
# CLI check: protoscalac's command line, its Makefile and its exit statuses
# (Phase 7 Task 4 Step 6).
#
# Usage: transpiler-cli.sh <protoscala> <tests-dir> <protoscalac> <scratch-dir>
set -u
PROTOSCALA="${1:?usage: transpiler-cli.sh <protoscala> <tests-dir> <protoscalac> <scratch>}"
TESTS_DIR="${2:?}"
PROTOSCALAC="${3:?}"
SCRATCH="${4:?}"

fails=0
check() {  # check <description> <expected-rc> <actual-rc>
    if [[ "$2" -ne "$3" ]]; then
        echo "FAIL: $1 (expected exit $2, got $3)"
        fails=$((fails + 1))
    fi
}

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

# No arguments: usage, exit 1.
"$PROTOSCALAC" >"$SCRATCH/noargs.out" 2>&1; check "no arguments" 1 $?
grep -q 'usage: protoscalac' "$SCRATCH/noargs.out" || {
    echo "FAIL: no arguments did not print usage"; fails=$((fails + 1)); }

# An unknown option: exit 1, and the message names the option.
"$PROTOSCALAC" --no-such-option x.scala >"$SCRATCH/opt.out" 2>&1; check "unknown option" 1 $?
grep -q "unknown option '--no-such-option'" "$SCRATCH/opt.out" || {
    echo "FAIL: unknown option was not named"; fails=$((fails + 1)); }

# A missing file: exit 1, and the message names the path.
"$PROTOSCALAC" "$SCRATCH/does-not-exist.scala" >"$SCRATCH/miss.out" 2>&1; check "missing file" 1 $?
grep -q 'does-not-exist.scala' "$SCRATCH/miss.out" || {
    echo "FAIL: missing file was not named"; fails=$((fails + 1)); }

# --as-module and --as-script together are refused rather than silently ordered.
"$PROTOSCALAC" --as-module --as-script "$TESTS_DIR/conformance/00-binary/hello-world.scala" \
    >"$SCRATCH/both.out" 2>&1; check "--as-module with --as-script" 1 $?
grep -q 'mutually exclusive' "$SCRATCH/both.out" || {
    echo "FAIL: the two modes were not refused together"; fails=$((fails + 1)); }

# --emit-cpp writes the C++ and nothing else.
HELLO="$TESTS_DIR/conformance/00-binary/hello-world.scala"
"$PROTOSCALAC" "$HELLO" -o "$SCRATCH/cpp" >"$SCRATCH/cpp.out" 2>&1; check "--emit-cpp" 0 $?
[[ -f "$SCRATCH/cpp/hello-world.cpp" ]] || {
    echo "FAIL: --emit-cpp wrote no hello-world.cpp"; fails=$((fails + 1)); }
[[ -f "$SCRATCH/cpp/Makefile" ]] && {
    echo "FAIL: --emit-cpp wrote a Makefile, which only --emit-make does"; fails=$((fails + 1)); }
grep -q 'proto_module_init' "$SCRATCH/cpp/hello-world.cpp" || {
    echo "FAIL: the generated C++ has no proto_module_init"; fails=$((fails + 1)); }
# The whole point of the P1 rule: no generated C++ local holds a value. Every
# ProtoObject* in the file is a function parameter or the `S`/`self` binding.
if grep -nE 'const proto::ProtoObject\* [a-z][A-Za-z0-9_]* =' "$SCRATCH/cpp/hello-world.cpp"; then
    echo "FAIL: a generated C++ local holds a const proto::ProtoObject* (P1)"
    fails=$((fails + 1))
fi
# And no `catch`: the two boundary sites live in libprotoScala.so (D6), and a block with
# no protected region emits no retry loop at all -- which is also the zero-cost property.
# A `catch` CLAUSE, not the word: a fixture whose own file name contains "catch"
# appears in the banner comment and in every #line directive.
catch_clauses() { grep -cE '^[[:space:]]*(\} )?catch[[:space:]]*\(' "$1"; }
if [[ "$(catch_clauses "$SCRATCH/cpp/hello-world.cpp")" != "0" ]]; then
    echo "FAIL: the generated C++ contains a catch clause (D6 keeps them in the library)"
    fails=$((fails + 1))
fi

# The positive counterpart, without which the check above would pass on a transpiler
# that had no exceptions at all. A fixture WITH a `try` must emit exactly ONE
# `catch (...)` per guarded block -- one per BLOCK, never one per `try` -- and its
# clauses must still live in the library, so the only catch is the loop's.
TRY_FIXTURE="$TESTS_DIR/conformance/20-exceptions/finally-runs-when-catch-does-not-match.scala"
mkdir -p "$SCRATCH/try"
if "$PROTOSCALAC" "$TRY_FIXTURE" -o "$SCRATCH/try" >"$SCRATCH/try.out" 2>&1; then
    gen=$(ls "$SCRATCH/try"/*.cpp 2>/dev/null | head -1)
    if [[ -z "$gen" ]]; then
        echo "FAIL: --emit-cpp on a try/catch fixture wrote no .cpp"
        fails=$((fails + 1))
    else
        n=$(grep -cE '^[[:space:]]*(\} )?catch[[:space:]]*\(\.\.\.\)' "$gen")
        total=$(catch_clauses "$gen")
        # One retry loop per guarded block. This fixture has one such block.
        if [[ "$n" != "1" || "$total" != "1" ]]; then
            echo "FAIL: expected exactly one 'catch (...)' and no other catch; got $n and $total"
            grep -n 'catch' "$gen" | sed 's/^/  /'
            fails=$((fails + 1))
        fi
        # The handler search reads `pc`, so the tracking must be there.
        grep -q '^ *pc = [0-9]' "$gen" || {
            echo "FAIL: the guarded block emits no pc tracking, so the handler search would"
            echo "      read a stale pc"
            fails=$((fails + 1)); }
        # And the switch is INSIDE the try: C++ forbids jumping into a try block, and a
        # switch placed outside it would not compile -- but it would also not be caught
        # here, so the order of the two lines is asserted.
        if ! awk '/^ *try \{/{t=NR} /switch \(resumePc\)/{s=NR} END{exit !(t && s && s > t)}' "$gen"; then
            echo "FAIL: the resume switch is not inside the try block"
            fails=$((fails + 1))
        fi
    fi
else
    echo "FAIL: protoscalac refused a try/catch fixture"
    sed 's/^/  /' "$SCRATCH/try.out"
    fails=$((fails + 1))
fi

# A module includes protoCore.h and protoScala/GeneratedModule.h, and protoscalac must
# REFUSE when it cannot find them on its own include path rather than let the compiler
# find some other copy.
#
# This is not hypothetical. Measured on an installation whose prefix held no protoCore
# headers: the generated Makefile's single `-I<prefix>/include` ADDS to the compiler's
# default path rather than replacing it, so `#include <protoCore.h>` resolved to
# /usr/local/include/protoCore.h -- a copy from February, of a different protoCore. The
# module compiled, linked, loaded, and segfaulted in `ProtoObject::newChild`. A refusal
# here is the only place that can be caught.
mkdir -p "$SCRATCH/hdr"
out=$(PROTOSCALAC_INCLUDE_DIRS=/nonexistent "$PROTOSCALAC" "$HELLO" -o "$SCRATCH/hdr" \
        --emit-make 2>&1)
rc=$?
if [[ $rc -eq 0 ]]; then
    echo "FAIL: protoscalac accepted an include path with no protoCore.h"
    fails=$((fails + 1))
fi
grep -q 'cannot find protoCore.h in its include path' <<<"$out" || {
    echo "FAIL: the missing-header refusal does not name protoCore.h:"
    sed 's/^/  /' <<<"$out"; fails=$((fails + 1)); }
grep -q 'PROTOSCALAC_INCLUDE_DIRS' <<<"$out" || {
    echo "FAIL: the refusal does not say how to fix it"; fails=$((fails + 1)); }
# And nothing was written: the same rule the source refusals obey.
if compgen -G "$SCRATCH/hdr/Makefile" >/dev/null; then
    echo "FAIL: the refusal left a Makefile behind"
    fails=$((fails + 1))
fi

# --emit-make writes the Makefile, and its LIBS line is exactly the two libraries.
"$PROTOSCALAC" "$HELLO" -o "$SCRATCH/make" --emit-make >"$SCRATCH/make.out" 2>&1
check "--emit-make" 0 $?
if [[ "$(grep '^LIBS' "$SCRATCH/make/Makefile")" != "LIBS     = -lprotoScala -lprotoCore" ]]; then
    echo "FAIL: unexpected LIBS line: $(grep '^LIBS' "$SCRATCH/make/Makefile")"
    fails=$((fails + 1))
fi
# -Wl,-rpath for every library directory, so the module loads without
# LD_LIBRARY_PATH. That property is what Task 14 Step 5 verifies end to end.
grep -q -- '-Wl,-rpath,' "$SCRATCH/make/Makefile" || {
    echo "FAIL: the Makefile has no -Wl,-rpath entry"; fails=$((fails + 1)); }
# -O2, not -O3: a generated module is one long function per block.
grep -q '^CXXFLAGS = -O2 -fPIC -std=c++20$' "$SCRATCH/make/Makefile" || {
    echo "FAIL: unexpected CXXFLAGS: $(grep '^CXXFLAGS' "$SCRATCH/make/Makefile")"
    fails=$((fails + 1)); }

# A directory with whitespace is refused, because make splits words on it.
PROTOSCALAC_INCLUDE_DIRS="/a b" "$PROTOSCALAC" "$HELLO" -o "$SCRATCH/ws" --emit-make \
    >"$SCRATCH/ws.out" 2>&1
check "a whitespace include directory" 1 $?
grep -q 'whitespace, which make cannot handle' "$SCRATCH/ws.out" || {
    echo "FAIL: the whitespace message is missing"; fails=$((fails + 1)); }

# --build-so produces a loadable module, and the interpreter runs it.
"$PROTOSCALAC" "$HELLO" -o "$SCRATCH/so" --build-so >"$SCRATCH/so.out" 2>&1
check "--build-so" 0 $?
[[ -f "$SCRATCH/so/module.so" ]] || { echo "FAIL: --build-so wrote no module.so"; fails=$((fails + 1)); }
out=$("$PROTOSCALA" --run-module "$SCRATCH/so/module.so" 2>&1)
check "--run-module of the transpiled hello-world" 0 $?
if [[ "$out" != "Hello, protoScala!" ]]; then
    echo "FAIL: the transpiled hello-world printed '$out'"
    fails=$((fails + 1))
fi
# ldd states the cost honestly (§D2): the module brings a protoScala runtime.
if ! ldd "$SCRATCH/so/module.so" | grep -q 'libprotoScala.so.1'; then
    echo "FAIL: module.so does not name libprotoScala.so.1"
    fails=$((fails + 1))
fi

[[ $fails -eq 0 ]] || exit 1
echo OK
