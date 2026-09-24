// EXPECT-ERROR: Not found: Red
// Scala requires the qualified form `Color.Red` unless the case is imported, and
// protoScala does the same: no deviation is recorded, because matching Scala here
// costs nothing.
enum Color:
  case Red, Green
@main def run(): Unit = println(Red)
