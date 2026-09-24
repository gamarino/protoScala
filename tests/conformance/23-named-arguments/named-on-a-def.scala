// EXPECT: 11 11 11
def f(a: Int, b: Int): Int = a + b * 10
@main def run(): Unit =
  println(f(1, 1).toString + " " + f(a = 1, b = 1) + " " + f(b = 1, a = 1))
