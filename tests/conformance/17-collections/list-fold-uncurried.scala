// EXPECT: 6 6 7
// C11: the Scala spelling xs.foldLeft(z)(f) works through a one-shot applier,
// and the one-list spelling xs.foldLeft(z, f) is also accepted. Permissive, not
// divergent.
@main def run(): Unit =
  println(List(1, 2, 3).foldLeft(0)(_ + _).toString + " " +
    List(1, 2, 3).foldLeft(0, (a: Int, b: Int) => a + b) + " " +
    List[Int]().foldLeft(7)(_ + _))
