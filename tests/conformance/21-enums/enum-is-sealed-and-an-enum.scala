// EXPECT: true true true
// `Enum` is protoScala's own builtin marker trait; Scala's is
// `scala.reflect.Enum`, so this program does not compile under scalac as written
// (`Not found: type Enum`). The membership it checks is the same one.
enum Color:
  case Red, Green
@main def run(): Unit =
  val c: Color = Color.Red
  println(c.isInstanceOf[Color].toString + " " + c.isInstanceOf[Enum] + " " +
    (Color.values.head == Color.Red))
