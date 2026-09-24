// EXPECT-ERROR: conversions must follow a splice
// scalac rejects a bare % in an f literal with the same rule ("conversions must
// follow a splice; use %% for literal %"); verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val x = 1
  println(f"50% of $x")
