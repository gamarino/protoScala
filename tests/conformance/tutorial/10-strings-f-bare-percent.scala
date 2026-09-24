// EXPECT-ERROR: conversions must follow a splice
@main def run(): Unit =
  val x = 1
  println(f"50% of $x")
