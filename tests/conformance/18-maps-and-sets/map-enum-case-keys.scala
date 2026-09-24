// XFAIL: warm 2
// A singleton enum case is a case object, so DESIGN §6.1 bullet 1 makes it an
// identity key and `Color.Red` is the same key every time. Requires Phase 4's
// `enum`; the EXPECT line above is already the right one.
enum Color:
  case Red, Blue
@main def run(): Unit =
  val m = Map(Color.Red -> "warm", Color.Blue -> "cool")
  println(m(Color.Red) + " " + m.size)
