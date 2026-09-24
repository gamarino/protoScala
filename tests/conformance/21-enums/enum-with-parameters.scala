// EXPECT: 16711680 255 2
enum Color(val rgb: Int):
  case Red extends Color(0xFF0000)
  case Blue extends Color(0x0000FF)
@main def run(): Unit =
  println(Color.Red.rgb.toString + " " + Color.Blue.rgb + " " + Color.values.length)
