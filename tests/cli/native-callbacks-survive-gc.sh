#!/usr/bin/env bash
#
# CLI check: native methods that call back into Scala code, under collection.
#
# A native method runs in its caller's context, with no context of its own
# (src/runtime/CallerSlots.h, docs/VM.md section 5): what it holds across a
# callback is protected by the caller frame's young chain, by its arguments'
# slots, or by CallerSlots in the frame's spare slots, and a long loop hands
# the frame's garbage to the collector under a SafepointPermit. This program
# exercises each shape with callbacks that allocate: map/filter/foldLeft over
# Lists and Ranges, foreach, count, sortBy with a key function, a foldLeft
# inside a foldLeft callback, flatMap through Option, case-class copy with a
# named argument, an operator on a user class (a C++-initiated send), a Map
# built and folded, Try around a throwing block, String.map and exists. It
# runs under a 300,000-cell hard ceiling with
# PROTOCORE_GC_CONTEXT_THRESHOLD=200, so contexts submit their young chains
# at nearly every safepoint and collection cycles run while the natives hold
# their values: a value the collector reclaimed shows up as a wrong result or
# a crash. The expected line was produced by Scala 3.9.0 on the JVM (the same
# program, with `import scala.util.Try`).
#
# Sensitivity, measured 2026-10-25: with CallerSlots sabotaged to keep its
# slots in an untraced C++ array (while still allowing the submissions), this
# run crashed with SIGSEGV in 3 of 4 runs at 300,000 and 150,000 cells; at
# 1,000,000 cells it passed, which is why the ceiling is this low. The
# unsabotaged build passes at both (31 and 23 collection cycles).
#
# Usage: native-callbacks-survive-gc.sh <path-to-protoscala>
set -u
P="${1:?usage: native-callbacks-survive-gc.sh <protoscala>}"
work=$(mktemp -d -p "$PWD" native-callbacks.XXXXXX)
trap 'rm -rf "$work"' EXIT
cat >"$work/natives.scala" <<'SCALA'
case class P(x: Int, name: String)
class V(val n: Int):
  def +(o: V): V = new V(n + o.n)

def labels(n: Int): List[String] = (0 until n).map(i => "label-" + i).toList

@main def run(): Unit =
  val a = (0 until 30000).map(i => "x" + i).foldLeft(0)((acc, s) => acc + s.length)
  val b = labels(20000).filter(s => s.length % 2 == 0).map(s => s + s).foldLeft(0)(_ + _.length)
  var c = 0
  (1 to 20000).foreach(i => c += ("n" + i).length)
  val d = labels(15000).count(s => (s + "!").endsWith("7!"))
  val e = labels(6000).sortBy(s => s.reverse).head
  val f = (0 until 3000).toList.foldLeft(0)((acc, i) => acc + (0 until (i % 15)).map(j => "y" + j).foldLeft(0)(_ + _.length))
  val g = labels(10000).flatMap(s => if s.length > 9 then Some(s + "?") else None).length
  val h = (0 until 8000).map(i => P(i, "p" + i)).map(p => p.copy(name = p.name + "q")).foldLeft(0)((acc, p) => acc + p.name.length)
  val k = (0 until 8000).toList.foldLeft(new V(0))((acc, i) => acc + new V(("v" + i).length)).n
  val m = (0 until 5000).toList.foldLeft(Map.empty[String, Int])((mm, i) => mm + (("k" + (i % 300)) -> i)).foldLeft(0)((acc, kv) => acc + kv._1.length)
  val t = (0 until 5000).map(i => Try(if i % 3 == 0 then throw new RuntimeException("e" + i) else "ok" + i)).count(_.isSuccess)
  val s = (0 until 4000).map(i => ("s" + i).map(ch => ch.toUpper)).foldLeft(0)(_ + _.length)
  val w = labels(5000).exists(s => (s + "-").startsWith("label-4999-"))
  println(s"$a $b $c $d $e $f $g $h $k $m $t $s $w")
SCALA
expected="168890 181440 108894 1500 label-0 44000 9000 46890 38890 1090 3333 18890 true"
export PROTOCORE_HEAP_TRACE=1 PROTOCORE_ADAPTIVE_HEAP=0 PROTOCORE_HEAP_LIMIT_CELLS=300000
export PROTOCORE_GC_CONTEXT_THRESHOLD=200
out=$(timeout 300s "$P" "$work/natives.scala" 2>&1); rc=$?
if [[ $rc -ne 0 ]]; then
    echo "FAIL: protoscala exited $rc under PROTOCORE_HEAP_LIMIT_CELLS=300000"
    printf '%s\n' "$out" | grep -v '^protoCore heap:' | tail -5
    exit 1
fi
cycles=$(grep -c '^protoCore heap:' <<<"$out")
if (( cycles < 3 )); then
    echo "FAIL: expected at least 3 collection cycles, saw $cycles"
    exit 1
fi
result=$(printf '%s\n' "$out" | grep -v '^protoCore heap:' | tail -1)
if [[ "$result" != "$expected" ]]; then
    echo "FAIL: expected '$expected', got '$result'"
    exit 1
fi
echo "OK ($cycles cycles)"
