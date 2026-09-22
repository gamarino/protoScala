// EXPECT: Some(3) None true 3 0 Some(6) None Some(2) List(3) true
@main def run(): Unit =
  val s = Some(3)
  val n: Option[Int] = None
  println(s.toString + " " + n + " " + s.isDefined + " " + s.getOrElse(0) + " " + n.getOrElse(0) + " " +
    s.map(_ * 2) + " " + s.filter(_ > 5) + " " + s.flatMap(x => Some(x - 1)) + " " + s.toList + " " +
    (Some(1) == Some(1)))
