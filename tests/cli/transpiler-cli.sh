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
# And no `catch`: the two boundary sites live in libprotoScala.so (D6).
if grep -q 'catch' "$SCRATCH/cpp/hello-world.cpp"; then
    echo "FAIL: the generated C++ contains a catch clause (D6 keeps them in the library)"
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
