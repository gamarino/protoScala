// EXPECT: 42 -1
def parse(s: String): Int =
  try s.toInt
  catch case e: NumberFormatException => -1
@main def run(): Unit =
  println(parse("42").toString + " " + parse("x"))
