// EXPECT-ERROR: MatchError
// D4: types are erased, so a `match` that misses a case compiles and fails at run
// time. scalac rejects this program.
enum Shape:
  case Circle, Square
@main def run(): Unit =
  val s: Shape = Shape.Square
  println(s match { case Shape.Circle => "round" })
