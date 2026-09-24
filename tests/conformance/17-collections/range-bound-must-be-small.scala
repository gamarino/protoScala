// EXPECT-ERROR: a Range bound must fit a 54-bit integer
// D61: a bound must fit a SmallInteger. Matching Scala would mean boxing both
// bounds, an allocation on every Range method, to serve ranges of more than
// 2^53 elements.
@main def run(): Unit =
  val big = 9007199254740991L + 10
  println((0 until big).length)
