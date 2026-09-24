#!/usr/bin/env bash
#
# CLI check: allocating loops and recursion run under a low protoCore heap
# ceiling (DESIGN §10). The program reports the work it did and the check
# verifies it (benchmarks and stress tests self-report).
#
# Usage: gc-pressure.sh <path-to-protoscala> <tests-source-dir>
set -u
P="${1:?usage: gc-pressure.sh <protoscala> <tests-dir>}"
T="${2:?usage: gc-pressure.sh <protoscala> <tests-dir>}"
work=$(mktemp -d -p "$PWD" gc-pressure.XXXXXX)
trap 'rm -rf "$work"' EXIT
cat >"$work/strings.scala" <<'EOF'
@main def run(): Unit =
  var s = ""
  var i = 0
  while i < 20000 do
    s = s + "x"
    i += 1
  var total = 0
  var k = 0
  while k < 20000 do
    total += ("n" + k).length
    k += 1
  println(s.length.toString + " " + total)
EOF
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$work/strings.scala" 2>&1); rc=$?
# "n0".."n9999": 2*10 + 3*90 + 4*900 + 5*9000 + 6*10000 = 108890
[[ $rc -eq 0 && "$out" == "20000 108890" ]] || { echo "FAIL (strings): exit $rc, '$out'"; exit 1; }
# Closures over boxed vars, lazy vals and varargs lists, all allocated per
# iteration. Per i: the three x contribute -1 each; then f() - 3i - 2 is 3
# for an even i and 1 for an odd one, so the 50000 odd i add -2 each.
cat >"$work/closures.scala" <<'SCALA'
def all(xs: Int*) = xs
def mk(n: Int): () => Int =
  var c = n
  lazy val base = n * 2
  () => { c += 1; c + base }
@main def run(): Unit =
  var total = 0
  var i = 0
  while i < 100000 do
    val f = mk(i)
    val g = mk(i + 1)
    all(i, i + 1, i + 2).foreach { x =>
      total = total + (if x % 2 == 0 then f() - f() else g() - g())
    }
    total = total + f() - 3 * i - 2
    i += 1
  println(total)
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$work/closures.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "-100000" ]] || { echo "FAIL (closures): exit $rc, '$out'"; exit 1; }
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$T/conformance/06-recursion/fib-indent.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "6765" ]] || { echo "FAIL (fib): exit $rc, '$out'"; exit 1; }
# Object graphs (Phase 2): immutable case-class trees built, matched and
# dropped every round; a for-comprehension building tuples; a mutable
# instance updated in the loop. Expected values computed independently:
# the sum over rounds r = 0..39 of the leaf values (s % 7) of build(10, r),
# plus 4 tuples per round.
cat >"$work/objects.scala" <<'SCALA'
sealed trait Tree
case class Leaf(v: Int) extends Tree
case class Node(l: Tree, r: Tree) extends Tree
def build(d: Int, s: Int): Tree =
  if d == 0 then Leaf(s % 7) else Node(build(d - 1, 2 * s + 1), build(d - 1, 2 * s + 2))
def sum(t: Tree): Int = t match
  case Leaf(v) => v
  case Node(l, r) => sum(l) + sum(r)
class Counter:
  var n = 0
@main def run(): Unit =
  val c = new Counter
  var total = 0
  var round = 0
  while round < 40 do
    total += sum(build(10, round))
    val ps = for (x <- List(1, 2, 3, 4); y <- List(x, x + 1) if (x + y) % 2 == 1) yield (x, y)
    total += ps.length
    c.n += 1
    round += 1
  println(total.toString + " " + c.n)
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 90s "$P" "$work/objects.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "123037 40" ]] || { echo "FAIL (objects): exit $rc, '$out'"; exit 1; }
# Actors under a low heap: 200 actors, each receiving 200 case-class messages
# whose only reference is the mailbox, plus 200 asks whose futures are the only
# reference to the replies. 200 * 200 = 40000 messages; each actor ends at 200
# and the ask replies with that state, so the total is 200 * 200 = 40000.
# A lost message, a collected envelope or a collected reply gives a wrong
# total, not a crash -- this is a correctness check (DESIGN §8.5).
cat >"$work/actors.scala" <<'SCALA'
case class Add(n: Int)
var actors: List[Any] = Nil
var k = 0
while k < 200 do
  actors = Actor.spawn(0) { (s, m) =>
    m match
      case Add(n) => (s + n, s + n)
      case _      => (s, s)
  } :: actors
  k += 1
var round = 0
while round < 200 do
  actors.foreach(a => a ! Add(1))
  round += 1
var total = 0
actors.foreach(a => total += (a ? Add(0)).await)
println(total)
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 180s "$P" "$work/actors.scala" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "40000" ]] || { echo "FAIL (actors): exit $rc, '$out'"; exit 1; }

# --- Phase 3: Map under a low heap, value keys ---------------------------------
# 20000 entries whose only reference is the Map, then every key is read back. A
# key the collector reclaimed shows up as a miss, which is the whole point of
# ProtoMap's traced keys. The keys are built by concatenation, so the only
# reference to each is the slot (PROTOMAP-SPEC §5, now exercised from the
# language).
cat >"$work/map-pressure.scala" <<'SCALA'
@main def run(): Unit =
  var m = Map[String, Int]()
  var i = 0
  while i < 20000 do
    m = m + (("k" + i) -> i)
    i += 1
  var seen = 0
  var j = 0
  while j < 20000 do
    if m.getOrElse("k" + j, -1) == j then seen += 1
    j += 1
  println(seen.toString + " " + (if seen == 20000 then "ok" else "BAD"))
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 300s "$P" "$work/map-pressure.scala" 2>&1); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL (map pressure): exit $rc: $out"; exit 1; }
[[ "$out" == "20000 ok" ]] || { echo "FAIL (map pressure): printed '$out'"; exit 1; }

# --- Phase 3: Map under a low heap, IDENTITY keys ------------------------------
# The half ProtoMap's traced-key guarantee exists for: a value key lives inside
# the entry list, an identity key IS the slot key. 20000 instances of a class
# with the default equals, reachable only through the Map and the list that keeps
# them findable. If ProtoMap did not trace its keys the read-back count would
# fall below 20000 and print BAD.
cat >"$work/map-identity-pressure.scala" <<'SCALA'
class Key(val n: Int)
@main def run(): Unit =
  var m = Map[Key, Int]()
  var keep = List[Key]()
  var i = 0
  while i < 20000 do
    val k = new Key(i)
    m = m + (k -> i)
    keep = k :: keep
    i += 1
  var seen = 0
  var rest = keep
  while rest.nonEmpty do
    if m.getOrElse(rest.head, -1) == rest.head.n then seen += 1
    rest = rest.tail
  println(seen.toString + " " + (if seen == 20000 then "ok" else "BAD"))
SCALA
out=$(PROTOCORE_HEAP_LIMIT_CELLS=2000000 timeout 300s "$P" "$work/map-identity-pressure.scala" 2>&1); rc=$?
[[ $rc -eq 0 ]] || { echo "FAIL (identity-key pressure): exit $rc: $out"; exit 1; }
[[ "$out" == "20000 ok" ]] || { echo "FAIL (identity-key pressure): printed '$out'"; exit 1; }

echo OK
