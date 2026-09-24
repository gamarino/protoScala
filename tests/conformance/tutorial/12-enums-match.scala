// EXPECT: warm cool cool
enum Colour:
  case Red, Green, Blue
def describe(c: Colour): String = c match
  case Colour.Red   => "warm"
  case Colour.Green => "cool"
  case Colour.Blue  => "cool"
@main def run(): Unit =
  println(describe(Colour.Red) + " " + describe(Colour.Green) + " " + describe(Colour.Blue))
