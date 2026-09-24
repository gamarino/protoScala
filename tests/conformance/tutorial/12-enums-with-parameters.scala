// EXPECT: 16711680 255
enum Colour(val rgb: Int):
  case Red extends Colour(0xFF0000)
  case Blue extends Colour(0x0000FF)
@main def run(): Unit =
  println(Colour.Red.rgb.toString + " " + Colour.Blue.rgb)
