// EXPECT: 42 31 5 1000000 7 -8
@main def run(): Unit =
  println(42.toString + " " + 0x1F + " " + 0b101 + " " + 1_000_000 + " " + 7L + " " + -8)
