// EXPECT: 12 14 22
def f(a: Int = 1, b: Int = 2): Int = a + b * 10 - 9
@main def run(): Unit =
  println(f().toString + " " + f(a = 3) + " " + f(b = 3))
