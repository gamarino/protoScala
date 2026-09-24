// EXPECT: a 3 true false Some(a) None
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val m = Map(1 -> "a", 2 -> "b", 3 -> "c")
  println(m(1) + " " + m.size + " " + m.contains(2) + " " + m.contains(9) + " " +
    m.get(1) + " " + m.get(9))
