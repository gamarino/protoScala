// EXPECT: Red 0 Blue 2 List(Red, Green, Blue) 3
// D77: `values` returns a List, where Scala returns an Array — matching Scala
// would need an Array type this dialect does not have. Printed directly, scalac
// shows `[LColor;@...`, so the fixture compares the length and the elements.
enum Color:
  case Red, Green, Blue
@main def run(): Unit =
  println(Color.Red.toString + " " + Color.Red.ordinal + " " + Color.Blue + " " +
    Color.Blue.ordinal + " " + Color.values + " " + Color.values.length)
