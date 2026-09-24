// EXPECT: RED true
enum Color:
  case Red, Green
  def shout: String = toString.toUpperCase
@main def run(): Unit =
  println(Color.Red.shout + " " + (Color.Red == Color.Red))
