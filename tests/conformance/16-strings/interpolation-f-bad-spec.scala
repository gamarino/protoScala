// EXPECT-ERROR: unsupported format specifier
// %q is a compile error in scalac too (A0-2, D55).
@main def run(): Unit =
  val n = 1
  println(f"$n%q")
