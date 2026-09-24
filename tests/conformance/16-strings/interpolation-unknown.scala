// EXPECT-ERROR: value json is not a member of StringContext
// D56 retired (Phase 4): any interpolator that is not s, f or raw is lowered to
// `StringContext(<literals>).<name>(<args>)`, so an undefined one is a missing
// extension method on StringContext, reported when the send runs (D4). scalac
// rejects it at compile time with "value json is not a member of StringContext"
// — the same sentence, a different moment.
@main def run(): Unit =
  val n = 1
  println(json"value is $n")
