// EXPECT: 3 true false 4 List(2, 3) List(1, 2, 3)
@main def run(): Unit =
  val s = Set(1, 2, 3, 2, 1)
  println(s.size.toString + " " + s.contains(2) + " " + s(9) + " " + (s + 4).size + " " +
    (s - 1).toList.sorted + " " + s.toList.sorted)
