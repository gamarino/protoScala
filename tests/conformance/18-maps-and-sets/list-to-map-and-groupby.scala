// EXPECT: List((1,a), (2,b)) List((0,List(2, 4)), (1,List(1, 3)))
// groupBy keeps each group in the elements' own order. Verified against
// tools/scala3-3.9.0.
@main def run(): Unit =
  val pairs = List((1, "a"), (2, "b"))
  val xs = List(1, 2, 3, 4)
  println(pairs.toMap.toList.sortBy(_._1).toString + " " +
    xs.groupBy(_ % 2).toList.sortBy(_._1))
