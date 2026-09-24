// EXPECT: 1 2 List((b,2)) List((a,9), (b,2)) List((a,1), (c,3))
@main def run(): Unit =
  val counts = Map("a" -> 1)
  val more = counts + ("b" -> 2)
  println(counts.size.toString + " " + more.size + " " + (more - "a").toList + " " +
    more.updated("a", 9).toList.sortBy(_._1) + " " + (counts ++ Map("c" -> 3)).toList.sortBy(_._1))
