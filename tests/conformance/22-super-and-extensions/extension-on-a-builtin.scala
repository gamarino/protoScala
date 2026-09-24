// EXPECT: 6 SHOUT 3
// D83: an extension on a builtin type mutates that prototype for the whole
// session, so two units cannot define conflicting extensions of the same name on
// the same type.
extension (n: Int) def triple: Int = n * 3
extension (s: String) def shout: String = s.toUpperCase
extension (xs: List[Int]) def mySum: Int = xs.foldLeft(0)(_ + _)
@main def run(): Unit =
  println(2.triple.toString + " " + "shout".shout + " " + List(1, 2).mySum)
