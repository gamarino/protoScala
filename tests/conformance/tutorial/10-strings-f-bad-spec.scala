// EXPECT-ERROR: unsupported format specifier
@main def run(): Unit =
  val n = 1
  println(f"$n%q")
