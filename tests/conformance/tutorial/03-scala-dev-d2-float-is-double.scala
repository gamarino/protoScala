// EXPECT: 0.30000000000000004
// D2: Float is Double, so this sum is computed in double precision.
@main def run(): Unit =
  println(0.1f + 0.2f)
