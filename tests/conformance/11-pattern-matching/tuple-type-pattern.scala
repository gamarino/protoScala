// EXPECT: pair
// A parenthesised tuple type in a pattern tests the tuple's erasure only, as on
// the JVM (scalac 3.9 accepts it with an unchecked warning and prints `pair`).
@main def run(): Unit =
  val x: Any = (1, 2)
  x match
    case v: (Int, Int) => println("pair")
    case _             => println("no")
