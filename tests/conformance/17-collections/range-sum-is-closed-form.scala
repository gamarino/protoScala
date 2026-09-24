// EXPECT: 10 5050 499999999500000000
// sum is arithmetic, not iteration: a billion-element Range answers at once and
// the result promotes past a SmallInteger. scalac prints -1243309312 for the
// third field because its Int overflows; protoScala's Int is arbitrary
// precision (D1), so the exact value is the right answer here.
@main def run(): Unit =
  println((0 until 5).sum.toString + " " + (1 to 100).sum + " " + (0 until 1000000000).sum)
