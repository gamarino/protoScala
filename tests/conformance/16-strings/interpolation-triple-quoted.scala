// EXPECT: line1 x=7 line2
@main def run(): Unit =
  val x = 7
  val s = s"""line1 x=$x line2"""
  println(s)
