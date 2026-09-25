// EXPECT: Red
enum Colour:
  case Red, Green
import Colour.*
@main def run(): Unit = println(Red)
