// EXPECT: 4 0 Right(2) Left(missing) true
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val s: Option[Int] = Some(2)
  val n: Option[Int] = None
  println(s.fold(0)(_ * 2).toString + " " + n.fold(0)(_ * 2) + " " + s.toRight("missing") + " " +
    n.toRight("missing") + " " + s.zip(Some("a")).nonEmpty)
