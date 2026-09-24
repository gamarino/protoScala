// EXPECT: List(1, 2, 3, 4) List(3) List(1, 2) true
@main def run(): Unit =
  val a = Set(1, 2, 3)
  val b = Set(3, 4)
  println((a union b).toList.sorted.toString + " " + (a intersect b).toList.sorted + " " +
    (a diff b).toList.sorted + " " + Set(1, 2).subsetOf(a))
