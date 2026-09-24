// EXPECT: List((1,2), (2,4)) List((2,b)) 2 true
// A Map's function may be written `(k, v) => ...`; the arity of the value
// decides, so `p => p._1` works too. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val m = Map(1 -> 1, 2 -> 2)
  val s = Map(1 -> "a", 2 -> "b")
  println(m.map((k, v) => (k, v * 2)).toList.sortBy(_._1).toString + " " +
    s.filter((k, v) => k == 2).toList + " " + m.size + " " + m.forall((k, v) => v > 0))
