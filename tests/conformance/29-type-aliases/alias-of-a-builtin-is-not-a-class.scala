// EXPECT-ERROR: Not found: type X (an alias for Int)
// An alias is no more usable as a class than its right-hand side: `new X` on
// `type X = Int` fails, and the message names both the alias and its target.
// scalac rejects `new X` too ("Int does not have a constructor").
type X = Int

@main def run(): Unit = println(new X)
