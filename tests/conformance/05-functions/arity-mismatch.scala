// EXPECT-ERROR: wrong number of arguments
// D4: a compile-time error in Scala, a runtime error here.
@main def run(): Unit =
  val add = (a: Int, b: Int) => a + b
  println(add(1))
