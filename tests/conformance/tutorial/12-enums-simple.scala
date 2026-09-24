// EXPECT: Red 0 Blue 2 3
enum Colour:
  case Red, Green, Blue
@main def run(): Unit =
  println(Colour.Red.toString + " " + Colour.Red.ordinal + " " + Colour.Blue + " " +
    Colour.Blue.ordinal + " " + Colour.values.length)
