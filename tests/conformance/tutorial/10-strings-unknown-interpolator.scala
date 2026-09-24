// EXPECT-ERROR: value json is not a member of StringContext
// Phase 4 retired D56: an unknown interpolator is no longer a compile error but a
// missing extension method on StringContext, reported when the send runs (D4).
@main def run(): Unit =
  val n = 1
  println(json"value is $n")
