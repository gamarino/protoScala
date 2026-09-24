// EXPECT: true 1 0 null List(2) List(2)
@main def run(): Unit =
  val s: Option[Int] = Some(2)
  val n: Option[Int] = None
  println(n.forall(_ > 9).toString + " " + s.count(_ > 1) + " " + s.count(_ > 9) + " " +
    n.orNull + " " + s.iterator + " " + s.toSeq)
