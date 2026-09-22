// EXPECT: 5
// Types are annotations, like TypeScript or Python type hints; they are not checked (D4).
def add(a: Int, b: Int): Int = a + b
@main def run(): Unit =
  val total: Int = add(2, 3)
  println(total)
