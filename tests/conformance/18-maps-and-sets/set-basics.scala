// EXPECT: 3 true false List(1, 2, 3) 3
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val s = Set(1, 2, 3, 2, 1)
  println(s.size.toString + " " + s.contains(2) + " " + s(9) + " " + s.toList.sorted + " " +
    (s + 2).size)
