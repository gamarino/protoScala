// EXPECT-ERROR: MatchError: 5 (of class Int)
// D42: the Scala class of the unmatched value is named; the JVM names the
// boxed one (`scala.MatchError: 5 (of class java.lang.Integer)`).
@main def run(): Unit = {
  val r = 5 match {
    case 1 => "one"
  }
  println(r)
}
