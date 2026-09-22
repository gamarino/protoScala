// EXPECT: 10 7 42 3
@main def run(): Unit =
  val double = (x: Int) => x * 2
  val add = (a: Int, b: Int) => a + b
  val answer = () => 42
  val inc: Int => Int = x => x + 1
  println(double(5).toString + " " + add(3, 4) + " " + answer() + " " + inc(2))
