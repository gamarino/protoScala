// EXPECT: Red
// D79: a `derives` clause is parsed and ignored, as it is on every other
// template (D3): protoScala has no type classes to derive.
enum Color derives CanEqual:
  case Red, Green
@main def run(): Unit = println(Color.Red)
