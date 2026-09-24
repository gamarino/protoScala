// EXPECT: Vector(1, 2, 2, 3) List(1, 2, 3) List(1, 2) Vector(0, 1, 2) List((a,1), (b,2)) List((a,1)) List(1)
@main def run(): Unit =
  val xs = List(1, 2, 2, 3)
  println(xs.toVector.toString + " " + xs.toSet.toList.sorted + " " + Vector(1, 2).toList + " " +
    (0 until 3).toVector + " " + List(("a", 1), ("b", 2)).toMap.toList.sortBy(_._1) + " " +
    Map("a" -> 1).toList + " " + Set(1).toList)
