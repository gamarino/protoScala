#!/usr/bin/env bash
#
# CLI check: the actor workers are joined before the ProtoSpace is destroyed,
# on every exit path (DESIGN §8.2). A missed join shows up as a crash, a hang
# or protoCore noise on stderr during teardown.
#
# Usage: actors-shutdown.sh <path-to-protoscala>
set -u
P="${1:?usage: actors-shutdown.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" actors-shutdown.XXXXXX)
trap 'rm -rf "$work"' EXIT
mkdir "$work/home"

run_case() {   # name, expected-exit, expected-last-stdout-line, source
    local name="$1" want_rc="$2" want_out="$3" src="$4"
    printf '%s' "$src" > "$work/$name.scala"
    local out rc
    out=$(HOME="$work/home" timeout 60s "$P" "$work/$name.scala" 2>"$work/$name.err"); rc=$?
    local last; last=$(printf '%s' "$out" | tail -n 1)
    [[ $rc -eq $want_rc && "$last" == "$want_out" ]] || {
        echo "FAIL ($name): exit $rc (want $want_rc), last line '$last' (want '$want_out')"
        cat "$work/$name.err"; exit 1; }
}

# 1. Normal end with live actors and a full mailbox.
run_case normal 0 'done' '
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
var i = 0
while i < 1000 do
  a ! 1
  i += 1
println("done")
'
# 2. An uncaught error after actors were started.
run_case failing 1 '' '
val a = Actor.spawn(0) { (s, m) => (s, s) }
a ! 1
println(1 / 0)
'
# 3. An actor left suspended on a future that never completes: the process
#    still exits, and says so on stderr. An actor that asks *itself* builds
#    exactly that: the ask is queued behind the handler that is waiting for it,
#    and the single-method invariant means it can never be handled.
cat >"$work/suspended.scala" <<'SCALA'
var me: Any = null
val stuck = Actor.spawn(0) { (s, m) =>
  val v = (me ? m).await
  (v, v)
}
me = stuck
stuck ! 1
// Give the worker time to reach the suspension, then end the program with the
// actor still parked.
val t = System.nanoTime()
while System.nanoTime() - t < 300000000 do ()
println("started")
SCALA
out=$(HOME="$work/home" timeout 60s "$P" "$work/suspended.scala" 2>"$work/suspended.err"); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL (suspended): exit $rc"; cat "$work/suspended.err"; exit 1; }
grep -q "still waiting on a future" "$work/suspended.err" || {
    echo "FAIL (suspended): no diagnostic for the parked actor"; cat "$work/suspended.err"; exit 1; }
# 4. The REPL: actors created at the prompt, then :quit.
out=$(printf 'val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }\na ! 1\n:quit\n' \
      | HOME="$work/home" timeout 60s "$P" 2>"$work/repl.err"); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL (repl): exit $rc"; cat "$work/repl.err"; exit 1; }
[[ ! -e "$work/home/.protoscala_history" ]] || {
    echo "FAIL (repl): the check wrote a history file"; exit 1; }
echo OK
