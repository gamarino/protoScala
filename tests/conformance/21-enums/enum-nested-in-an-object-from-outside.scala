// EXPECT: Info
// The qualified spelling from outside the object, which needs the lifted name
// `E.Level` to carry its cases.
object E:
  enum Level:
    case Debug, Info
@main def run(): Unit = println(E.Level.Info)
