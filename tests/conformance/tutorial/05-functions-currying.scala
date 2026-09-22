// EXPECT: 15 15
def multiply(a: Int)(b: Int): Int = a * b
@main def run(): Unit =
  val triple = multiply(3)
  println(multiply(3)(5).toString + " " + triple(5))
