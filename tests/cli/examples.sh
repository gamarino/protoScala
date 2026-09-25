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
LR="$R/examples/log-report"

# The documented invocation: from the example's own directory, with no arguments.
out=$(cd "$LR" && timeout 60s "$P" Main.scala 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report: exit $rc, '$out'"; exit 1; }

# From elsewhere, the log is named. A module path is resolved against the
# importing file's directory; a DATA path is resolved against the working
# directory, here as on the JVM, so the two cases are checked separately.
out=$(timeout 60s "$P" "$LR/Main.scala" "$LR/sample.log" 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report named log: exit $rc, '$out'"; exit 1; }

# The fan width must not change the answer: that is the whole claim the actors
# in this example make, so it is checked rather than asserted in prose.
for width in 1 7; do
  out=$(timeout 60s "$P" "$LR/Main.scala" "$LR/sample.log" "$width" 2>&1); rc=$?
  last=$(printf '%s\n' "$out" | tail -1)
  [[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report width $width: exit $rc, '$out'"; exit 1; }
done

# The example resolves `report.Levels` and `report.Lines` relative to Main.scala,
# so it must run from a directory that has no `report/` of its own and with no
# PROTOSCALA_PATH set. Running it from / is the strictest form of that check.
out=$(cd / && env -u PROTOSCALA_PATH timeout 60s "$P" "$LR/Main.scala" "$LR/sample.log" 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$expected" ]] || { echo "FAIL log-report from /: exit $rc, '$out'"; exit 1; }

# The program must really OPEN the log, not carry its own copy of the text --
# which is what it did before protoScala had file I/O, with a diff between the
# two keeping them in step. Proof: run it against a COPY with one more ERROR line,
# and require a different report. A program parsing embedded text would print the
# same one.
#
# The copy is made in the working directory, which is the build tree when CTest
# runs this, and is removed on every exit path. Never /tmp.
work=$(mktemp -d -p "$PWD" examples.log-report.XXXXXX) || { echo "FAIL: mktemp"; exit 1; }
trap 'rm -rf "$work"' EXIT
cp "$LR/sample.log" "$work/edited.log" || { echo "FAIL: cp sample.log"; exit 1; }
printf 'ERROR db one more failure\n' >> "$work/edited.log"
edited_expected="Debug 2 | Info 5 | Warn 2 | Error 4 | malformed 2"
out=$(timeout 60s "$P" "$LR/Main.scala" "$work/edited.log" 2>&1); rc=$?
last=$(printf '%s\n' "$out" | tail -1)
[[ $rc -eq 0 && "$last" == "$edited_expected" ]] || {
  echo "FAIL log-report does not read the file it is given: exit $rc, '$out'"; exit 1; }

# A log that is not there must stop the program and say so, not report an empty
# log. The exit code and the message are both checked.
out=$(timeout 60s "$P" "$LR/Main.scala" "$work/there-is-no-such-log.log" 2>&1); rc=$?
[[ $rc -ne 0 ]] || { echo "FAIL log-report missing log: exit 0, '$out'"; exit 1; }
printf '%s\n' "$out" | grep -q "FileNotFoundException" || {
  echo "FAIL log-report missing log: no FileNotFoundException in '$out'"; exit 1; }

echo OK
