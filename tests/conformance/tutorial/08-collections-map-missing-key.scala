// EXPECT-ERROR: NoSuchElementException: key not found: Grace
@main def run(): Unit =
  println(Map("Ada" -> 36)("Grace"))
