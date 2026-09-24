// EXPECT: List(1, 2, 3, 4) List(2, 3) List(1) true false
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val a = Set(1, 2, 3)
  val b = Set(2, 3, 4)
  println((a union b).toList.sorted.toString + " " + (a intersect b).toList.sorted + " " +
    (a diff b).toList.sorted + " " + Set(1, 2).subsetOf(a) + " " + b.subsetOf(a))
