// EXPECT-ERROR: IllegalArgumentException: %d expects an integer, got String
// D4: scalac catches this at compile time because it has static types;
// protoScala is late-binding, so it is a run-time error that names what was
// expected and what arrived. No new deviation id.
@main def run(): Unit =
  val s = "x"
  println(f"$s%d")
