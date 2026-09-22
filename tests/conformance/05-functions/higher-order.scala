// EXPECT: 20 81
def twice(f: Int => Int, x: Int): Int = f(f(x))
def compose(f: Int => Int, g: Int => Int): Int => Int = x => f(g(x))
@main def run(): Unit =
  val sq = (x: Int) => x * x
  println(twice(x => x + 5, 10).toString + " " + compose(sq, x => x + 1)(8))
