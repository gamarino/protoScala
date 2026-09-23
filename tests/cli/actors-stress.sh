#!/usr/bin/env bash
#
# CLI check: the single-method invariant under contention (DESIGN §8.2).
# Every number here is exact: N sends leave the counter at N whatever the
# interleaving, so a lost, duplicated or concurrently-handled message shows up
# as a wrong number rather than as flakiness.
#
# Usage: actors-stress.sh <path-to-protoscala>
set -u
P="${1:?usage: actors-stress.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" actors-stress.XXXXXX)
trap 'rm -rf "$work"' EXIT

# 8 threads x 25000 sends of 1, then one ask of 0. The handler adds the message
# to the state and replies with the new state, so the reply is 200000 exactly.
cat >"$work/race.scala" <<'SCALA'
val counter = Actor.spawn(0) { (s, m) => (s + m, s + m) }
val ts = List(1, 2, 3, 4, 5, 6, 7, 8).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < 25000 do
      counter ! 1
      i += 1
  }
}
ts.foreach(t => t.join())
println((counter ? 0).await)
SCALA
# 500 actors x (20 sends of 1 + one ask of 0): each actor ends at 20, and the
# ask replies with that state, so the total is 500 * 20 = 10000.
cat >"$work/many.scala" <<'SCALA'
var actors: List[Any] = Nil
var k = 0
while k < 500 do
  actors = Actor.spawn(0) { (s, m) => (s + m, s + m) } :: actors
  k += 1
var round = 0
while round < 20 do
  actors.foreach(a => a ! 1)
  round += 1
var total = 0
actors.foreach(a => total += (a ? 0).await)
println(total)
SCALA
for w in 1 2 8 16; do
    out=$(PROTOSCALA_ACTOR_WORKERS=$w timeout 180s "$P" "$work/race.scala" 2>&1)
    [[ "$out" == "200000" ]] || { echo "FAIL (race, workers=$w): '$out'"; exit 1; }
    out=$(PROTOSCALA_ACTOR_WORKERS=$w timeout 180s "$P" "$work/many.scala" 2>&1)
    [[ "$out" == "10000" ]] || { echo "FAIL (many, workers=$w): '$out'"; exit 1; }
done
echo OK
