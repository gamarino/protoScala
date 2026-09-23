// EXPECT-ERROR: MatchError: 5 (of class Int)
@main def run(): Unit = {
  val r = 5 match {
    case 1 => "one"
  }
  println(r)
}
