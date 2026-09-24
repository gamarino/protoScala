// EXPECT: Range 4 to 0 by -1 Range 9 to 0 by -3 List(4, 3, 2, 1, 0)
// scalac answers a Range, not a sequence, and matching it costs four words.
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println((0 until 5).reverse.toString + " " + (0 until 10 by 3).reverse + " " +
    (0 until 5).reverse.toList)
