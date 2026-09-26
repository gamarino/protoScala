#!/usr/bin/env bash
#
# protoScala DIFFERENTIAL conformance runner (Phase 7 Task 11).
#
# Runs one `.scala` fixture through `protoscalac` instead of the interpreter:
# transpile, compile the generated C++ with the generated Makefile, load the
# resulting `.so` with `protoscala --run-module`, and judge the result against the
# fixture's own first-line directive. The directive parser and the two verdict
# functions are COPIED VERBATIM from run.sh, so the two harnesses cannot disagree
# about what a fixture asks for.
#
# Usage:
#   run-transpiled.sh <protoscala> <protoscalac> <file.scala> <exclude-file> <scratch-dir>
#
# Three anti-rot guards, each present because its absence is a way for this
# harness to pass while proving nothing:
#
#   1. A C++ COMPILE FAILURE IS ALWAYS A FAIL, never a pass. Without this, an
#      EXPECT-ERROR fixture whose expected substring happened to appear in a g++
#      diagnostic would go green on a broken generator.
#   2. A TRANSPILE REFUSAL IS A FAIL unless the fixture is on the exclusion list,
#      and then it is a pass with the reason printed.
#   3. A FIXTURE ON THE EXCLUSION LIST THAT NOW WORKS IS A FAIL. This is the
#      bidirectional check that keeps the list from rotting, and it is the same
#      discipline the XFAIL directive already uses.
#
# Temporary files live under the scratch directory given, never in /tmp.

set -u

PROTOSCALA="${1:?usage: run-transpiled.sh <protoscala> <protoscalac> <file.scala> <exclude-file> <scratch-dir>}"
PROTOSCALAC="${2:?}"
FILE="${3:?}"
EXCLUDE_FILE="${4:?}"
SCRATCH="${5:?}"

if [[ ! -x "$PROTOSCALA" ]]; then
    echo "FAIL: protoscala binary not executable: $PROTOSCALA"
    exit 1
fi
if [[ ! -x "$PROTOSCALAC" ]]; then
    echo "FAIL: protoscalac binary not executable: $PROTOSCALAC"
    exit 1
fi
if [[ ! -f "$FILE" ]]; then
    echo "FAIL: test file not found: $FILE"
    exit 1
fi

# --- the directive, parsed exactly as run.sh parses it ----------------------
first_line=$(head -n 1 "$FILE")
directive=""
expected=""
case "$first_line" in
    "// XFAIL-ERROR: "*) directive="XFAIL-ERROR";  expected="${first_line#// XFAIL-ERROR: }" ;;
    "// XFAIL-ERROR"*)   directive="XFAIL-ERROR";  expected="" ;;
    "// XFAIL: "*)       directive="XFAIL";        expected="${first_line#// XFAIL: }" ;;
    "// EXPECT-ERROR: "*) directive="EXPECT-ERROR"; expected="${first_line#// EXPECT-ERROR: }" ;;
    "// EXPECT-ERROR"*)  directive="EXPECT-ERROR"; expected="" ;;
    "// EXPECT: "*)      directive="EXPECT";       expected="${first_line#// EXPECT: }" ;;
    *)
        echo "FAIL: no recognised directive in first line of $FILE"
        echo "  first line was: $first_line"
        exit 1
        ;;
esac

# --- the exclusion list ------------------------------------------------------
# Format: "<relative fixture path>  <reason-code>  <reason>". A `#` line is a
# comment. The relative path is matched as a SUFFIX of the fixture path, so the
# list is written the way a reader writes a fixture name.
excluded_reason=""
if [[ -f "$EXCLUDE_FILE" ]]; then
    while read -r path code reason; do
        [[ -z "${path:-}" || "${path:0:1}" == "#" ]] && continue
        case "$FILE" in
            *"/$path") excluded_reason="$code  $reason"; break ;;
        esac
    done < "$EXCLUDE_FILE"
fi

mkdir -p "$SCRATCH" || { echo "FAIL: cannot create scratch directory $SCRATCH"; exit 1; }
rm -f "$SCRATCH"/*.cpp "$SCRATCH"/*.o "$SCRATCH"/module.so "$SCRATCH"/Makefile 2>/dev/null

stdout_file="$SCRATCH/stdout"
stderr_file="$SCRATCH/stderr"
: >"$stdout_file"
: >"$stderr_file"

FILE_DIR=$(cd "$(dirname "$FILE")" && pwd)
FILE_BASE=$(basename "$FILE")
ABS_FILE="$FILE_DIR/$FILE_BASE"

ms_now() { date +%s%3N; }

# --- stage 1: transpile ------------------------------------------------------
#
# Two kinds of non-zero exit, and telling them apart is what lets an EXPECT-ERROR
# fixture pass on this path at all:
#
#   * a REFUSAL -- protoscalac declines to transpile a program the interpreter
#     runs. That is a harness FAIL unless the fixture is on the exclusion list.
#   * a COMPILE ERROR -- the program is wrong and protoscalac's front end (which
#     IS the interpreter's front end) says so. For 158 of the fixtures that is the
#     point of the fixture, so the directive is judged against protoscalac's own
#     diagnostic, exactly as run.sh judges it against the interpreter's.
t0=$(ms_now)
transpile_out="$SCRATCH/transpile.log"
if ! timeout 120s "$PROTOSCALAC" "$ABS_FILE" -o "$SCRATCH" --emit-make >"$transpile_out" 2>&1; then
    if grep -qE 'not supported by protoscalac|not supported in a transpiled module' \
            "$transpile_out"; then
        if [[ -n "$excluded_reason" ]]; then
            echo "transpiled EXCLUDED: $FILE_BASE  ($excluded_reason)"
            exit 0
        fi
        echo "FAIL: protoscalac refused $FILE_BASE and it is not on the exclusion list"
        echo "  add it to $(basename "$EXCLUDE_FILE") with a reason code, or fix the generator"
        sed 's/^/  /' "$transpile_out"
        exit 1
    fi
    # A diagnostic about the program itself. Judge the directive against it.
    cat "$transpile_out" >"$stderr_file"
    exit_code=1
    last_line=""
    if [[ "$directive" == "EXPECT-ERROR" ]]; then
        if [[ -z "$expected" ]] || grep -q -F -- "$expected" "$stderr_file"; then
            if [[ -n "$excluded_reason" ]]; then
                echo "FAIL: $FILE_BASE is excluded as $excluded_reason but now works — remove it from $(basename "$EXCLUDE_FILE")"
                exit 1
            fi
            echo "transpiled OK: $FILE_BASE (rejected at compile time, as the fixture expects)"
            exit 0
        fi
    fi
    if [[ "$directive" == "XFAIL" || "$directive" == "XFAIL-ERROR" ]]; then
        echo "transpiled OK: $FILE_BASE (XFAIL: still fails, as recorded)"
        exit 0
    fi
    if [[ -n "$excluded_reason" ]]; then
        echo "transpiled EXCLUDED: $FILE_BASE  ($excluded_reason)"
        exit 0
    fi
    echo "FAIL: protoscalac rejected $FILE_BASE, which the fixture does not expect"
    sed 's/^/  /' "$transpile_out"
    exit 1
fi
t1=$(ms_now)

# A fixture on the exclusion list that TRANSPILES is not yet a pass: the list may
# name a run-time difference rather than a refusal, so the verdict is taken below
# and guard 3 fires only if the whole pipeline succeeds.

# --- stage 2: compile --------------------------------------------------------
compile_out="$SCRATCH/compile.log"
if ! ( cd "$SCRATCH" && timeout 300s make ) >"$compile_out" 2>&1; then
    # Guard 1: a C++ compile failure is a harness FAIL whatever the directive
    # says. An EXPECT-ERROR fixture must not be able to pass on a g++ diagnostic.
    echo "FAIL: the generated C++ did not compile ($FILE_BASE)"
    sed 's/^/  /' "$compile_out"
    exit 1
fi
t2=$(ms_now)

# --- stage 3: run ------------------------------------------------------------
# The same working-directory rules the interpreted twin runs under, so a fixture
# that reads a file or imports a module sees what it saw before.
if [[ -n "${PROTOSCALA_RUN_CWD:-}" ]]; then
    if [[ ! -d "$PROTOSCALA_RUN_CWD" ]]; then
        echo "FAIL: PROTOSCALA_RUN_CWD is not a directory: $PROTOSCALA_RUN_CWD"
        exit 1
    fi
    RUN_DIR="$PROTOSCALA_RUN_CWD"
else
    RUN_DIR="$FILE_DIR"
fi
( cd "$RUN_DIR" && timeout 90s "$PROTOSCALA" --run-module "$SCRATCH/module.so" ) \
    >"$stdout_file" 2>"$stderr_file"
exit_code=$?
t3=$(ms_now)

last_line=$(awk 'NF{ last=$0 } END{ print last }' "$stdout_file")

matches_expect() {
    [[ $exit_code -eq 0 && "$last_line" == "$expected" ]]
}
matches_expect_error() {
    [[ $exit_code -ne 0 ]] || return 1
    [[ -z "$expected" ]] && return 0
    grep -q -F -- "$expected" "$stderr_file" "$stdout_file"
}

verdict_ok=1
case "$directive" in
    EXPECT)       matches_expect       && verdict_ok=0 ;;
    EXPECT-ERROR) matches_expect_error && verdict_ok=0 ;;
    XFAIL)        matches_expect       || verdict_ok=0 ;;
    XFAIL-ERROR)  matches_expect_error || verdict_ok=0 ;;
esac

if [[ -n "$excluded_reason" && $verdict_ok -eq 0 ]]; then
    # Guard 3: the list must not rot.
    echo "FAIL: $FILE_BASE is excluded as $excluded_reason but now works — remove it from $(basename "$EXCLUDE_FILE")"
    exit 1
fi
if [[ -n "$excluded_reason" ]]; then
    echo "transpiled EXCLUDED: $FILE_BASE  ($excluded_reason)"
    exit 0
fi

if [[ $verdict_ok -eq 0 ]]; then
    # Self-reporting, because a harness whose numbers are all zero is a broken
    # harness that looks like a clean suite.
    echo "transpiled OK: $FILE_BASE (transpile $((t1 - t0)) ms, compile $((t2 - t1)) ms, run $((t3 - t2)) ms)"
    exit 0
fi

echo "FAIL: transpiled $FILE_BASE disagreed with its directive ($directive)"
echo "  exit $exit_code, expected ${expected:+'}${expected}${expected:+'}"
echo "  last stdout line: '$last_line'"
echo "stdout:"; sed 's/^/  /' "$stdout_file"
echo "stderr:"; sed 's/^/  /' "$stderr_file"
exit 1
