// EXPECT: refused refused refused refused refused refused
// Thread.start takes any value whose class answers `apply` -- a function, a
// Map, a Vector, a List -- and refuses every value whose class does not, at
// the call, whatever protoCore kind holds it: Int, Double, Char, Boolean,
// Unit and null.
@main def run(): Unit =
  val values = List[Any](42, 2.5, 'c', true, (), null)
  println(values.map(v =>
    try { Thread.start(v); "accepted" }
    catch { case e: IllegalArgumentException => "refused" }).mkString(" "))
