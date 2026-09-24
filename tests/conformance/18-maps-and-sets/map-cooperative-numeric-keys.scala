// EXPECT: a a a 1
// Scala's cooperative numeric equality makes 1, 1L and 1.0 the same key, which
// falls out of using scalaHash and valuesEqual as the KeySemantics. Verified
// against tools/scala3-3.9.0 (whose static types need the key type spelled Any).
@main def run(): Unit =
  val m = Map[Any, String](1 -> "a")
  println(m(1) + " " + m(1L) + " " + m(1.0) + " " + (m + (1.0 -> "a")).size)
