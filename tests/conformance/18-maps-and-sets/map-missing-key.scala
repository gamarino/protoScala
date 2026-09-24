// EXPECT-ERROR: NoSuchElementException: key not found: 9
// scalac's message text, pinned exactly.
@main def run(): Unit =
  println(Map(1 -> "a")(9))
