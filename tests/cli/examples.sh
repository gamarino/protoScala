#!/usr/bin/env bash
#
# CLI check: the shipped examples run and print what they compute.
#
# Usage: examples.sh <path-to-protoscala> <repo-root>
set -u
P="${1:?usage: examples.sh <protoscala> <repo-root>}"
R="${2:?usage: examples.sh <protoscala> <repo-root>}"
out=$(timeout 60s "$P" "$R/examples/hello.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "Hello, protoScala!" ]] || { echo "FAIL hello: exit $rc, '$out'"; exit 1; }
out=$(timeout 60s "$P" "$R/examples/fib.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "fib(25) = 75025" ]] || { echo "FAIL fib: exit $rc, '$out'"; exit 1; }
out=$(timeout 60s "$P" "$R/examples/fib.scala" 10 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "fib(10) = 55" ]] || { echo "FAIL fib 10: exit $rc, '$out'"; exit 1; }

# log-report: the worked example. Only the LAST line is compared, because the
# report is the program's one deterministic output; anything the runtime writes
# before it (a warning on stderr, say) must not turn into a false failure.
expected="Debug 2 | Info 5 | Warn 2 | Error 3 | malformed 2"
out=$(timeout 60s "$P" "$R/examples/log-report/Main.scala" 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report: exit $rc, '$out'"; exit 1; }

# The fan width must not change the answer: that is the whole claim the actors
# in this example make, so it is checked rather than asserted in prose.
out=$(timeout 60s "$P" "$R/examples/log-report/Main.scala" 1 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report width 1: exit $rc, '$out'"; exit 1; }
out=$(timeout 60s "$P" "$R/examples/log-report/Main.scala" 7 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report width 7: exit $rc, '$out'"; exit 1; }

# The example resolves `report.Levels` and `report.Lines` relative to Main.scala,
# so it must run from a directory that has no `report/` of its own and with no
# PROTOSCALA_PATH set. Running it from / is the strictest form of that check.
out=$(cd / && env -u PROTOSCALA_PATH timeout 60s "$P" "$R/examples/log-report/Main.scala" 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report from /: exit $rc, '$out'"; exit 1; }

# protoScala has no file I/O, so examples/log-report/report/Sample.scala carries
# the text of sample.log between two `"""` markers. Diff the two here: a reader
# who edits the .log file and not the module would otherwise see the old report
# with no warning at all.
embedded=$(sed -n '/^val text: String = """$/,/^"""$/p' "$R/examples/log-report/report/Sample.scala" | sed '1d;$d')
if ! diff -q <(printf '%s\n' "$embedded") "$R/examples/log-report/sample.log" >/dev/null; then
  echo "FAIL log-report: report/Sample.scala and sample.log have drifted apart"
  diff <(printf '%s\n' "$embedded") "$R/examples/log-report/sample.log"
  exit 1
fi

echo OK
