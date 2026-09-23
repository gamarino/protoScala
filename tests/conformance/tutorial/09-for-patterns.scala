// EXPECT: List(a1, b2) List(1, 3) List(4, 6)
@main def run(): Unit =
  val pairs = List(("a", 1), ("b", 2))
  val joined = for ((s, n) <- pairs) yield s + n
  val present = for (case Some(v) <- List(Some(1), None, Some(3))) yield v
  val big = for (x <- List(1, 2, 3); y = x * 2 if y > 2) yield y
  println(joined.toString + " " + present + " " + big)
