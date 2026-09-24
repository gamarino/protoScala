// EXPECT-ERROR: Not found: Red
// A case is named `Colour.Red`, exactly as in Scala; the bare name resolves only
// inside the enum's own body and its companion.
enum Colour:
  case Red, Green
@main def run(): Unit = println(Red)
