// EXPECT: Red Green
// Inside the enum's own body and its companion, a case resolves without the
// qualifier, which is Scala's scoping.
enum Color:
  case Red, Green
  def other: Color = if ordinal == 0 then Green else Red
@main def run(): Unit =
  println(Color.Green.other.toString + " " + Color.Red.other)
