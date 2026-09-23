// EXPECT: max other exact
val Max = 10
@main def run(): Unit = {
  val target = 7
  def check(n: Int) = n match {
    case Max => "max"
    case `target` => "exact"
    case _ => "other"
  }
  println(check(10) + " " + check(3) + " " + check(7))
}
