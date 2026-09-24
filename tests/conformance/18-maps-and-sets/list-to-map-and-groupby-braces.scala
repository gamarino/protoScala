// EXPECT: List((1,a), (2,b)) List((0,List(2, 4)), (1,List(1, 3)))
// The brace-syntax twin of list-to-map-and-groupby.scala.
@main def run(): Unit = {
  val pairs = List((1, "a"), (2, "b"))
  val xs = List(1, 2, 3, 4)
  println(pairs.toMap.toList.sortBy(_._1).toString + " " +
    xs.groupBy(_ % 2).toList.sortBy(_._1))
}
