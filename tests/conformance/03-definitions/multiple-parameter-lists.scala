// EXPECT: 7 7
def add(a: Int)(b: Int): Int = a + b
@main def run(): Unit =
  val add3 = add(3)
  println(add(3)(4).toString + " " + add3(4))
