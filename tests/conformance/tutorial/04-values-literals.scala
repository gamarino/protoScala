// EXPECT: 255 1000000 2.5 1.0E-4 x true
@main def run(): Unit =
  println(0xFF.toString + " " + 1_000_000 + " " + 2.5 + " " + 0.0001 + " " + 'x' + " " + true)
