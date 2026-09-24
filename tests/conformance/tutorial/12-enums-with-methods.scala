// EXPECT: RED true
enum Colour:
  case Red, Green
  def shout: String = toString.toUpperCase
@main def run(): Unit =
  println(Colour.Red.shout + " " + (Colour.Red == Colour.Red))
