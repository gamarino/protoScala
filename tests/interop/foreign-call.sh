#!/usr/bin/env bash
#
# The cross-runtime call (Phase 7 §D3, docs/INTEROP.md §8).
#
# Transpiles tests/interop/Exports.scala into a shared library, then calls four of its
# functions from a translation unit that includes protoCore.h and one shim header and
# names nothing of protoScala. Three things are checked, in this order:
#
#   1. the foreign half really is foreign -- a grep, because the property is the claim;
#   2. the interpreted path agrees with the transpiled one on the same four calls;
#   3. the foreign caller gets the right answers.
#
# Usage: foreign-call.sh <protoscala> <protoscalac> <foreign_caller> <src-dir> <scratch>
set -u
PROTOSCALA="${1:?usage: foreign-call.sh <protoscala> <protoscalac> <caller> <src> <scratch>}"
PROTOSCALAC="${2:?}"
CALLER="${3:?}"
SRC="${4:?}"
SCRATCH="${5:?}"

fails=0

# 1. The foreign half names nothing of protoScala. Only the banner comment may, and it
#    is the first block of the file, so the check starts after it.
body=$(sed '1,/^#include <protoCore.h>$/d' "$SRC/foreign_caller.cpp")
if grep -qi 'protoscala' <<<"$body"; then
    echo "FAIL: the foreign caller names protoScala:"
    grep -in 'protoscala' <<<"$body" | sed 's/^/  /'
    fails=$((fails + 1))
fi
# The shim header likewise: the foreign side includes it, so it must pull in protoCore
# and nothing else. Its own comment explains what it is for and may say the name.
others=$(grep -c '^#include' "$SRC/ForeignHost.h")
if [[ "$others" != "1" ]] || ! grep -q '^#include <protoCore.h>$' "$SRC/ForeignHost.h"; then
    echo "FAIL: ForeignHost.h includes something other than protoCore.h"
    grep '^#include' "$SRC/ForeignHost.h" | sed 's/^/  /'
    fails=$((fails + 1))
fi

case "$SCRATCH" in
    /home/*/Documentos/proyectos/*) rm -rf "$SCRATCH" ;;
    *) echo "FAIL: refusing to clear '$SCRATCH'"; exit 1 ;;
esac
mkdir -p "$SCRATCH"

# 2. The interpreted answers, from the same source file. This is the differential: the
#    numbers below are not written by hand anywhere, they are what the interpreter says.
cp "$SRC/Exports.scala" "$SCRATCH/interpreted.scala"
cat >> "$SCRATCH/interpreted.scala" <<'SCALA'

println(add(3, 4))
println(sumTo(10))
println(greet("world"))
try { println(reciprocal(0)) } catch { case e: RuntimeException => println("raised") }
SCALA
interpreted=$("$PROTOSCALA" "$SCRATCH/interpreted.scala" 2>&1)
expected=$'7\n55\nhello, world\nraised'
if [[ "$interpreted" != "$expected" ]]; then
    echo "FAIL: the interpreter does not agree with the values the caller asserts:"
    echo "  got:      $(tr '\n' '|' <<<"$interpreted")"
    echo "  expected: $(tr '\n' '|' <<<"$expected")"
    fails=$((fails + 1))
fi

# 3. The transpiled module, and the foreign call into it.
if ! "$PROTOSCALAC" "$SRC/Exports.scala" -o "$SCRATCH" --build-so >"$SCRATCH/build.out" 2>&1; then
    echo "FAIL: protoscalac could not build the module"
    sed 's/^/  /' "$SCRATCH/build.out"
    exit 1
fi
out=$("$CALLER" "$SCRATCH/module.so" 2>&1)
rc=$?
if [[ $rc -ne 0 || "$out" != "OK" ]]; then
    echo "FAIL: the foreign caller exited $rc:"
    sed 's/^/  /' <<<"$out"
    fails=$((fails + 1))
fi

[[ $fails -eq 0 ]] || exit 1
echo OK
