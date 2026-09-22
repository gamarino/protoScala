// EXPECT: 120
def fact(n: Int): Int =
  def loop(i: Int, acc: Int): Int =
    if i > n then acc else loop(i + 1, acc * i)
  loop(1, 1)
@main def run(): Unit = println(fact(5))
