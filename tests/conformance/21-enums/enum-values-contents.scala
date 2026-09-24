// EXPECT: Red,Green,Blue 3
// The elements and the order of `values`, which is what D77 leaves comparable
// with scalac: only the container differs (a List here, an Array there).
enum Color:
  case Red, Green, Blue
@main def run(): Unit =
  println(Color.values.map(_.toString).mkString(",") + " " + Color.values.length)
