// EXPECT: enum Color has no case with name: Purple | enum Color has no case with ordinal: 9
// The generated messages are scalac's own, verified against it.
enum Color:
  case Red, Green, Blue
@main def run(): Unit =
  val a = try { Color.valueOf("Purple"); "?" } catch case e: IllegalArgumentException => e.getMessage
  val b = try { Color.fromOrdinal(9); "?" } catch case e: NoSuchElementException => e.getMessage
  println(a + " | " + b)
