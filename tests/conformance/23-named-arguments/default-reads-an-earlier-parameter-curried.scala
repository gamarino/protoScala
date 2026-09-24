// EXPECT: 1 2 5 6
// The spelling scalac accepts, for the comparison D88 rests on: a default that
// reads a parameter of a PREVIOUS list. Desugar folds the extra list into a
// lambda, and the default lands on that lambda, so protoScala answers the same.
def f(a: Int)(b: Int = a + 1): String = a.toString + " " + b
@main def run(): Unit =
  println(f(1)() + " " + f(5)())
