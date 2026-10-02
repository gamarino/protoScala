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
# shellcheck source=platform.sh
source "$(dirname "$0")/platform.sh"

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
refuse a_default "a parameter with a default value is not supported by protoscalac yet (D121)" \
    'def f(a: Int, b: Int = 2) = a + b' \
    '@main def run(): Unit = println(f(1))'
refuse an_import "an import is not supported by protoscalac yet (D123)" \
    'import util.Strings' \
    '@main def run(): Unit = println(1)'

# The counterpart of `refuse`: a construct the transpiler SUPPORTS must produce a module
# that prints what the interpreter prints. Without this the file would only ever say
# what protoscalac cannot do, and a refusal added by mistake would read as correct.
accept() {  # accept <name> <expected-stdout> <source...>
    local name="$1" want="$2"; shift 2
    printf '%s\n' "$@" > "$SCRATCH/src/$name.scala"
    local dir="$SCRATCH/$name"
    mkdir -p "$dir"
    if ! "$PROTOSCALAC" "$SCRATCH/src/$name.scala" -o "$dir" --build-so \
            >"$dir/build.out" 2>&1; then
        echo "FAIL: $name was refused; it must be accepted:"
        sed 's/^/  /' "$dir/build.out"
        fails=$((fails + 1))
        return
    fi
    local got
    got=$("$PROTOSCALA" --run-module "$dir/module.$SO" 2>&1)
    if [[ "$got" != "$want" ]]; then
        echo "FAIL: $name printed '$got', expected '$want'"
        fails=$((fails + 1))
        return
    fi
    # And the interpreter agrees, so the expectation above is not this file's opinion.
    got=$("$PROTOSCALA" "$SCRATCH/src/$name.scala" 2>&1)
    if [[ "$got" != "$want" ]]; then
        echo "FAIL: $name interpreted printed '$got', expected '$want'"
        fails=$((fails + 1))
    fi
}

# D118 was closed on 2026-09-27. The case that used to assert the refusal is kept as
# its positive counterpart, deliberately: the exclusion list's anti-rot guard treats a
# listed exclusion that now works as a FAIL, and this is the same rule applied by hand
# to the one message that was removed.
accept a_class "1" \
    'class Point(val x: Int)' \
    '@main def run(): Unit = println(new Point(1).x)'
accept an_object_with_a_trait "hello from Greeter" \
    'trait Greeter:' \
    '  def name: String' \
    '  def greet: String = "hello from " + name' \
    'object G extends Greeter:' \
    '  def name = "Greeter"' \
    '@main def run(): Unit = println(G.greet)'
# D120 was closed on 2026-09-27, and the `a_try` refusal case becomes three positive
# ones -- the same hand-applied version of the exclusion list's anti-rot rule that
# retired `a_class`. The third is the case that is easy to get wrong and was worth 11
# fixtures: an exception raised inside a handler, caught by an ENCLOSING try in the same
# frame, which only works because the frame keeps ONE retry loop for its whole body.
accept a_try "1" \
    '@main def run(): Unit =' \
    '  try println(1) catch case e: Throwable => println(2)'
accept a_finally "body cleanup" \
    '@main def run(): Unit =' \
    '  try print("body ") finally println("cleanup")'
accept a_second_exception "inner outer" \
    '@main def run(): Unit =' \
    '  try' \
    '    try throw new RuntimeException("a")' \
    '    catch' \
    '      case e: RuntimeException =>' \
    '        print("inner ")' \
    '        throw new IllegalStateException("b")' \
    '  catch' \
    '    case e: IllegalStateException => println("outer")'
accept a_case_class "Point(1,2) 3" \
    'case class Point(x: Int, y: Int)' \
    '@main def run(): Unit =' \
    '  val p = Point(1, 2)' \
    '  println(p.toString + " " + (p.x + p.y))'

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
