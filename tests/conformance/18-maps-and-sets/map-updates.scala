// EXPECT: 2 1 z a
// A Map is immutable: `m + (k -> v)` returns a new map and leaves `m` alone.
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val m = Map(1 -> "a")
  val m2 = m + (2 -> "b")
  val m3 = m2 - 2
  val m4 = m.updated(1, "z")
  println(m2.size.toString + " " + m3.size + " " + m4(1) + " " + m(1))
