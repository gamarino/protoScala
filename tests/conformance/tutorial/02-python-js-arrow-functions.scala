// EXPECT: 14 12
@main def run(): Unit =
  val double = (x: Int) => x * 2          // JS: x => x * 2, Python: lambda x: x * 2
  def applyTwice(f: Int => Int, x: Int): Int = f(f(x))
  println(double(7).toString + " " + applyTwice(double, 3))
