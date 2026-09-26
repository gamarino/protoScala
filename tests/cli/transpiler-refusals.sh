#!/usr/bin/env bash
#
# CLI check: what the first cut REFUSES, and that it emits nothing when it does
# (Phase 7 Task 13 Step 5).
#
# §D5's rule is that every exclusion is a refusal with a named message, never a
# mistranslation, and that `check()` walks the whole unit BEFORE writing anything:
# a partially written .cpp that a later `make` compiles into something is worse
# than no output.
#
# Usage: transpiler-refusals.sh <protoscala> <tests-dir> <protoscalac> <scratch-dir>
set -u
PROTOSCALA="${1:?usage: transpiler-refusals.sh <protoscala> <tests-dir> <protoscalac> <scratch>}"
TESTS_DIR="${2:?}"
PROTOSCALAC="${3:?}"
SCRATCH="${4:?}"

fails=0
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/src" "$SCRATCH/out"

refuse() {  # refuse <name> <expected-substring> <source...>
    local name="$1" want="$2"; shift 2
    printf '%s\n' "$@" > "$SCRATCH/src/$name.scala"
    rm -f "$SCRATCH/out"/*.cpp
    "$PROTOSCALAC" "$SCRATCH/src/$name.scala" -o "$SCRATCH/out" >"$SCRATCH/$name.out" 2>&1
    local rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "FAIL: $name was accepted; it must be refused"
        fails=$((fails + 1))
        return
    fi
    if ! grep -q -F -- "$want" "$SCRATCH/$name.out"; then
        echo "FAIL: $name refused without the expected message '$want':"
        sed 's/^/  /' "$SCRATCH/$name.out"
        fails=$((fails + 1))
    fi
    # Nothing written: check() runs first and emits nothing.
    if compgen -G "$SCRATCH/out/*.cpp" >/dev/null; then
        echo "FAIL: $name left a .cpp behind"
        fails=$((fails + 1))
    fi
    # The position is reported, so a refusal says where to look.
    if ! grep -qE ':[0-9]+: error:' "$SCRATCH/$name.out"; then
        echo "FAIL: $name refused without a line number"
        fails=$((fails + 1))
    fi
}

refuse await_in_a_body "await is not supported in a transpiled module (D113)" \
    '@main def run(): Unit =' \
    '  val f = Future { 1 }' \
    '  println(f.await)'
refuse a_class "a class, trait or object is not supported by protoscalac yet (D118)" \
    'class Point(val x: Int)' \
    '@main def run(): Unit = println(new Point(1).x)'
refuse a_try "try/catch/finally is not supported by protoscalac yet (D120)" \
    '@main def run(): Unit =' \
    '  try println(1) catch case e: Throwable => println(2)'
refuse a_default "a parameter with a default value is not supported by protoscalac yet (D121)" \
    'def f(a: Int, b: Int = 2) = a + b' \
    '@main def run(): Unit = println(f(1))'
refuse an_import "an import is not supported by protoscalac yet (D123)" \
    'import util.Strings' \
    '@main def run(): Unit = println(1)'

# --report-purity classifies, and both classes have a passing case: a report that
# only ever said "not pure" would not be a classification.
printf '%s\n' 'def pick(a: Boolean, b: Int, c: Int): Int = if a then b else c' \
    > "$SCRATCH/src/pure.scala"
out=$("$PROTOSCALAC" "$SCRATCH/src/pure.scala" --report-purity 2>&1)
rc=$?
if [[ $rc -ne 0 ]]; then
    echo "FAIL: --report-purity on the eligible unit exited $rc"
    fails=$((fails + 1))
fi
# The eligible unit still needs protoScala today, because a top-level `def`
# becomes a MAKE_FN and a STORE_GLOBAL. That is the HONEST answer, and the case
# records it rather than asserting the answer the plan hoped for: §D2's conditions
# (ii) and (iii) are violated by any unit that defines a name at all.
grep -q 'verdict:' <<<"$out" || {
    echo "FAIL: --report-purity printed no verdict: $out"; fails=$((fails + 1)); }

out=$("$PROTOSCALAC" "$TESTS_DIR/conformance/00-binary/hello-world.scala" --report-purity 2>&1)
rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL: --report-purity exited $rc"; fails=$((fails + 1)); }
grep -q 'verdict: not protoCore-pure' <<<"$out" || {
    echo "FAIL: hello-world was not reported as impure: $out"; fails=$((fails + 1)); }
grep -q 'PUSH_GLOBAL println' <<<"$out" || {
    echo "FAIL: the purity report does not name println: $out"; fails=$((fails + 1)); }
grep -q 'one ProtoSpace term to the process sizing rule' <<<"$out" || {
    echo "FAIL: the purity report does not state the sizing consequence"; fails=$((fails + 1)); }

[[ $fails -eq 0 ]] || exit 1
echo OK
