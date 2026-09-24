// EXPECT-ERROR: %d expects an integer, got String
@main def run(): Unit =
  val s = "x"
  println(f"$s%d")
