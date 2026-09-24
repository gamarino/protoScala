// EXPECT: true true true Some(4) 2 1 (List(2, 4),List(1, 3, 5))
@main def run(): Unit =
  val xs = List(1, 2, 3, 4, 5)
  println(xs.contains(3).toString + " " + xs.exists(_ > 4) + " " + xs.forall(_ > 0) + " " +
    xs.find(_ > 3) + " " + xs.count(_ % 2 == 0) + " " + xs.indexOf(2) + " " +
    xs.partition(_ % 2 == 0))
