// EXPECT: List(3) List() Some(6) None None true 0
@main def run(): Unit =
  val found: Option[Int] = Some(3)
  val missing: Option[Int] = None
  println(found.toList.toString + " " + missing.toList + " " + found.map(_ * 2) + " " +
    missing.map(_ * 2) + " " + found.filter(_ > 5) + " " + found.exists(_ > 2) + " " +
    missing.getOrElse(0))
