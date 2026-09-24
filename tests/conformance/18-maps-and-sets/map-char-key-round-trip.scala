// EXPECT: 1 1 1 true
// The C1 ruling in both directions: inserted as a Char, found as an Int and as a
// Char; inserted as an Int, found as a Char. A Char left on the identity path
// would make one of these print -1 or false. Verified against
// tools/scala3-3.9.0.
@main def run(): Unit =
  val byChar = Map[Any, Int]('a' -> 1)
  val byInt = Map[Any, Int](97 -> 1)
  println(byChar.getOrElse('a', -1).toString + " " + byChar.getOrElse(97, -1) + " " +
    byInt.getOrElse('a', -1) + " " + (Set[Any]('a', 97).size == 1))
