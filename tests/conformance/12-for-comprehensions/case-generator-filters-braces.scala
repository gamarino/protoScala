// EXPECT: List(1, 3) List(1, 3)
// The brace-syntax twin of case-generator-filters.scala.
@main def run(): Unit = {
  val mixed = List(1, (1, 2), 4, (3, 1))
  val pairs = for { case (a, b) <- mixed } yield a
  val somes = for { case Some(v) <- List(Some(1), None, Some(3)) } yield v
  println(pairs.toString + " " + somes)
}
