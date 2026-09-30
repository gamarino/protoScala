#!/usr/bin/env bash
#
# CLI check: a program as a Unix filter. sys.exit(n) sets the exit status (and
# nothing after it runs), the script's arguments reach @main, output written
# with println and output of a child run with `!` arrive in program order, and a
# filter piped into `head` stops quietly when `head` has seen enough.
#
# Usage: io-program.sh <path-to-protoscala>
set -u
P="${1:?usage: io-program.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" io-program.XXXXXX)
trap 'rm -rf "$work"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# 1. sys.exit with a status, from the top level and from inside a method.
printf 'println("before")\nsys.exit(3)\nprintln("after")\n' >"$work/exit3.scala"
out=$("$P" "$work/exit3.scala" 2>&1); rc=$?
[[ $rc -eq 3 && "$out" == "before" ]] || fail "sys.exit(3): exit $rc, output '$out'"
printf 'def stop(): Unit = sys.exit(0)\nprint("x")\nstop()\nprintln("after")\n' >"$work/exit0.scala"
out=$("$P" "$work/exit0.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "x" ]] || fail "sys.exit(0): exit $rc, output '$out'"
# From an actor's handler: the whole program ends, not only the actor.
cat >"$work/exit-actor.scala" <<'SCALA'
val a = Actor.spawn(0) { (s, m) => sys.exit(4); (s, s) }
a ! 1
var i = 0
while i < 200000000 do i += 1
println("not reached")
SCALA
out=$(timeout 60s "$P" "$work/exit-actor.scala" 2>&1); rc=$?
[[ $rc -eq 4 && "$out" != *"not reached"* ]] || fail "sys.exit in an actor: exit $rc, output '$out'"

# 2. Arguments reach @main, and the program's exit status follows sys.exit.
cat >"$work/args.scala" <<'SCALA'
@main def run(args: String*): Unit =
  println(args.mkString(","))
  sys.exit(args.length)
SCALA
out=$("$P" "$work/args.scala" a 'b c' 2>&1); rc=$?
[[ $rc -eq 2 && "$out" == "a,b c" ]] || fail "args: exit $rc, output '$out'"

# 3. Output order: println, then a child writing to the same stdout, then println.
printf 'println("a")\nProcess(List("echo", "b")).!\nprintln("c")\n' >"$work/order.scala"
out=$("$P" "$work/order.scala" | cat); rc=$?
[[ $rc -eq 0 && "$out" == $'a\nb\nc' ]] || fail "output order: exit $rc, output '$out'"

# 4. A filter in a pipeline: `head -1` closes the pipe early and the program
#    stops quietly (no error report, no hang).
cat >"$work/filter.scala" <<'SCALA'
for line <- Source.stdin.getLines() do
  println(line.toUpperCase)
SCALA
out=$(seq 1 200000 | sed 's/^/x/' | timeout 60s "$P" "$work/filter.scala" 2>"$work/err" | head -1)
[[ "$out" == "X1" ]] || fail "pipeline: output '$out'"
[[ ! -s "$work/err" ]] || fail "pipeline: unexpected stderr: $(cat "$work/err")"

# 5. stderr from a failed `!!` reaches the program's stderr, not its stdout.
printf 'try Process(List("sh", "-c", "echo oops >&2; exit 1")).!!\ncatch case e: RuntimeException => println("caught")\n' >"$work/stderr.scala"
out=$("$P" "$work/stderr.scala" 2>"$work/err2"); rc=$?
[[ $rc -eq 0 && "$out" == "caught" && "$(cat "$work/err2")" == "oops" ]] ||
    fail "stderr of !!: exit $rc, stdout '$out', stderr '$(cat "$work/err2")'"

# 6. Tutorial chapter 18 §18.2: the usage message goes to stderr, nothing to
#    stdout, and the shell sees status 2; with a name it greets.
tut="$(cd "$(dirname "$0")/../conformance/tutorial" && pwd)"
out=$("$P" "$tut/18-io-usage-and-exit.scala" 2>"$work/err3"); rc=$?
[[ $rc -eq 2 && -z "$out" && "$(cat "$work/err3")" == "usage: greet NAME..." ]] ||
    fail "tutorial §18.2: exit $rc, stdout '$out', stderr '$(cat "$work/err3")'"
out=$(SHELL=/bin/sh "$P" "$tut/18-io-usage-and-exit.scala" Ada 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "Hello, Ada, from /bin/sh" ]] || fail "tutorial §18.2 with a name: exit $rc, output '$out'"

echo OK
