// EXPECT: true false true 2 Some(4) 1 (List(2, 4),List(1, 3, 5))
// The brace-syntax twin of list-predicates.scala.
@main def run(): Unit = {
  val xs = List(1, 2, 3, 4, 5)
  println(xs.contains(3).toString + " " + xs.forall(_ > 1) + " " + xs.exists(_ > 4) + " " +
    xs.count(_ % 2 == 0) + " " + xs.find(_ > 3) + " " + xs.indexOf(2) + " " +
    xs.partition(_ % 2 == 0))
}
