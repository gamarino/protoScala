// EXPECT: 1 2 5 6
// A default may read the parameters declared before it IN THE SAME LIST, because
// the default block takes exactly those slots as its own arguments.
//
// D88: scalac rejects this — it requires the referenced parameter to come from a
// PREVIOUS parameter list (`def f(a: Int)(b: Int = a + 1)` compiles and prints
// the same line, verified). Matching that restriction would mean tracking the
// original parameter-list boundaries that Desugar has already folded into
// lambdas, for the sake of rejecting a program a reader finds perfectly clear.
def f(a: Int, b: Int = a + 1): String = a.toString + " " + b
@main def run(): Unit =
  println(f(1) + " " + f(5))
