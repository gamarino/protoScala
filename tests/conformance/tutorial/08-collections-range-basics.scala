// EXPECT: Range 0 until 5 List(0, 1, 2, 3, 4) List(1, 2, 3, 4, 5) List(0, 3, 6, 9) 5 5050
@main def run(): Unit =
  println((0 until 5).toString + " " + (0 until 5).toList + " " + (1 to 5).toList + " " +
    (0 until 10 by 3).toList + " " + (0 until 5).length + " " + (1 to 100).sum)
