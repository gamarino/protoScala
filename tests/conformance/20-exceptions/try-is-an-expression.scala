// EXPECT: 42 -1
@main def run(): Unit =
  val good = try "42".toInt catch case e: NumberFormatException => -1
  val bad = try "x".toInt catch case e: NumberFormatException => -1
  println(good.toString + " " + bad)
