// EXPECT: List((1,a), (2,b), (3,c)) List(1, 2, 3) List(a, b, c)
// Map iteration order is unspecified (D7, D58), so a fixture always sorts: the
// suite pins behaviour and never pins the order. Verified against
// tools/scala3-3.9.0.
@main def run(): Unit =
  val m = Map(3 -> "c", 1 -> "a", 2 -> "b")
  println(m.toList.sortBy(_._1).toString + " " + m.keys.toList.sorted + " " +
    m.values.toList.sorted)
