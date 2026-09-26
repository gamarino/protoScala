// EXPECT-ERROR: MatchError
// The other half of D35: written *without* `case`, a tuple pattern generator
// is trusted to meet tuples, so a non-tuple element raises MatchError rather
// than being discarded. scalac rejects this program at compile time instead
// ("pattern's type is more specialized"), so there is no runtime answer to
// match; protoScala has no static types and fails at the element.
@main def run(): Unit =
  val mixed = List(1, (1, 2))
  println((for ((a, b) <- mixed) yield a).toString)
