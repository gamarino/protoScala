// EXPECT: List(1, 3) List(1, 3)
// Scala 3's `case` on a generator discards the elements that do not match.
// A tuple pattern is no exception: the list holds plain integers as well as
// pairs, and scalac yields List(1, 3) for both comprehensions (verified with
// tools/scala3-3.9.0). protoScala used to raise `MatchError: 1 (of class Int)`
// for the tuple pattern while filtering correctly for `case Some(v)` -- the
// inconsistency D35 now records as resolved.
@main def run(): Unit =
  val mixed = List(1, (1, 2), 4, (3, 1))
  val pairs = for (case (a, b) <- mixed) yield a
  val somes = for (case Some(v) <- List(Some(1), None, Some(3))) yield v
  println(pairs.toString + " " + somes)
