// EXPECT: neg zero pos
def sign(n: Int): String =
  if (n < 0)
    "neg"
  else if (n == 0)
    "zero"
  else
    "pos"
@main def run(): Unit =
  println(sign(-5) + " " + sign(0) + " " + sign(7))
