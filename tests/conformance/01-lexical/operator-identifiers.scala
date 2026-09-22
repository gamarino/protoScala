// EXPECT: 10 x_+ ok
def +++(a: Int, b: Int): Int = a * 2 + b * 2
def x_+(s: String): String = s + " ok"
@main def run(): Unit =
  println(+++(2, 3).toString + " " + x_+("x_+"))
