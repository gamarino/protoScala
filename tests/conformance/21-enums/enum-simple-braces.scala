// EXPECT: Red 0 Blue 2 List(Red, Green, Blue) 3
enum Color {
  case Red, Green, Blue
}
@main def run(): Unit = {
  println(Color.Red.toString + " " + Color.Red.ordinal + " " + Color.Blue + " " +
    Color.Blue.ordinal + " " + Color.values + " " + Color.values.length)
}
