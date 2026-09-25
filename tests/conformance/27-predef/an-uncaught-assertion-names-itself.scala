// EXPECT-ERROR: AssertionError: assertion failed: index out of range
// What a user actually sees: an assertion nobody catches ends the program with a
// non-zero status and names itself on stderr.
@main def run(): Unit =
  assert(false, "index out of range")
