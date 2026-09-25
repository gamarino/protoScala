#!/usr/bin/env bash
#
# protoScala conformance-fixture runner.
#
# Black-box: runs one `.scala` program through the `protoscala` binary in its
# own process, captures stdout/stderr and the exit code, and compares the
# result against a directive in the first line of the file (same pattern as
# protoST and protoClojure).
#
# Usage:
#   run.sh <path-to-protoscala> <test-file.scala>
#
# Directive format (first line, a `//` comment followed by the directive):
#
#   // EXPECT: <text>        exit 0 and the last non-empty stdout line equals
#                            <text> exactly.
#   // EXPECT-ERROR          non-zero exit.
#   // EXPECT-ERROR: <text>  non-zero exit and stderr/stdout contains <text>.
#   // XFAIL: <text>         expected to fail today; the body is the
#   // XFAIL-ERROR[: <text>] spec-correct EXPECT / EXPECT-ERROR. The verdict is
#                            inverted, and the runner fails loudly when the
#                            program unexpectedly conforms (remove the marker).
#
# Temporary files live in the current directory (CTest runs inside the build
# tree), never in /tmp.

set -u

PROTOSCALA="${1:?usage: run.sh <protoscala> <file.scala>}"
FILE="${2:?usage: run.sh <protoscala> <file.scala>}"

if [[ ! -x "$PROTOSCALA" ]]; then
    echo "FAIL: protoscala binary not executable: $PROTOSCALA"
    exit 1
fi
if [[ ! -f "$FILE" ]]; then
    echo "FAIL: test file not found: $FILE"
    exit 1
fi

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

WORK_DIR="$PWD"
stdout_file=$(mktemp -p "$WORK_DIR" conformance.out.XXXXXX)
stderr_file=$(mktemp -p "$WORK_DIR" conformance.err.XXXXXX)
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

# Run from the fixture's directory so relative imports resolve. A wall-clock
# timeout catches infinite loops without hanging the suite.
#
# PROTOSCALA_RUN_CWD overrides the working directory (Track F). A fixture that
# demonstrates file I/O must name its files the way a reader would -- `"notes.txt"`,
# not a path assembled from an environment variable -- and a relative path is
# resolved against the working directory. The fixture's own directory is the SOURCE
# tree, which no test may write into, so the tutorial's file fixtures are given a
# private directory in the build tree and run from there. Module resolution is
# unaffected: a module path is resolved against the importing FILE's directory
# first, which is why the script is named absolutely here.
FILE_DIR=$(cd "$(dirname "$FILE")" && pwd)
FILE_BASE=$(basename "$FILE")
# The script is named RELATIVELY when it is run from its own directory, because a
# diagnostic quotes the path it was given and several fixtures match on
# `<file>:<line>: error:`.
if [[ -n "${PROTOSCALA_RUN_CWD:-}" ]]; then
    if [[ ! -d "$PROTOSCALA_RUN_CWD" ]]; then
        echo "FAIL: PROTOSCALA_RUN_CWD is not a directory: $PROTOSCALA_RUN_CWD"
        exit 1
    fi
    ( cd "$PROTOSCALA_RUN_CWD" && timeout 90s "$PROTOSCALA" "$FILE_DIR/$FILE_BASE" ) \
        >"$stdout_file" 2>"$stderr_file"
else
    ( cd "$FILE_DIR" && timeout 90s "$PROTOSCALA" "$FILE_BASE" ) \
        >"$stdout_file" 2>"$stderr_file"
fi
exit_code=$?

last_line=$(awk 'NF{ last=$0 } END{ print last }' "$stdout_file")

matches_expect() {
    [[ $exit_code -eq 0 && "$last_line" == "$expected" ]]
}
matches_expect_error() {
    [[ $exit_code -ne 0 ]] || return 1
    [[ -z "$expected" ]] && return 0
    grep -q -F -- "$expected" "$stderr_file" "$stdout_file"
}

case "$directive" in
    EXPECT)
        if matches_expect; then exit 0; fi
        echo "FAIL: exit $exit_code, expected last line '$expected', got '$last_line'"
        echo "stdout:"; sed 's/^/  /' "$stdout_file"
        echo "stderr:"; sed 's/^/  /' "$stderr_file"
        exit 1
        ;;
    EXPECT-ERROR)
        if matches_expect_error; then exit 0; fi
        echo "FAIL: exit $exit_code, expected an error${expected:+ containing '$expected'}"
        echo "stdout:"; sed 's/^/  /' "$stdout_file"
        echo "stderr:"; sed 's/^/  /' "$stderr_file"
        exit 1
        ;;
    XFAIL)
        if matches_expect; then
            echo "FAIL: XFAIL passed unexpectedly — remove the XFAIL marker"
            exit 1
        fi
        exit 0
        ;;
    XFAIL-ERROR)
        if matches_expect_error; then
            echo "FAIL: XFAIL-ERROR passed unexpectedly — remove the XFAIL marker"
            exit 1
        fi
        exit 0
        ;;
esac

echo "FAIL: internal: directive parsing fell through"
exit 1
