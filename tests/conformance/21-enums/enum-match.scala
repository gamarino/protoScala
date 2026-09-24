// EXPECT: warm cool cool
enum Color:
  case Red, Green, Blue
def describe(c: Color): String = c match
  case Color.Red => "warm"
  case Color.Green => "cool"
  case Color.Blue => "cool"
@main def run(): Unit =
  println(describe(Color.Red) + " " + describe(Color.Green) + " " + describe(Color.Blue))
