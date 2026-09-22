// EXPECT: List(a1, b2) List(1, 3)
@main def run(): Unit =
  val pairs = List(("a", 1), ("b", 2))
  val xs = for ((s, n) <- pairs) yield s + n
  val ys = for (case Some(v) <- List(Some(1), None, Some(3))) yield v
  println(xs.toString + " " + ys)
