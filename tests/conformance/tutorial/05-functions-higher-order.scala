// EXPECT: 16 10
def compose(f: Int => Int, g: Int => Int): Int => Int = x => f(g(x))
@main def run(): Unit =
  val square = (x: Int) => x * x
  val inc = (x: Int) => x + 1
  println(compose(square, inc)(3).toString + " " + compose(inc, square)(inc(2)))
