// EXPECT: sum=3 first=a swapped=(2,1) 3-tuple
def f(v: Any): String = v match {
  case (a: Int, b: Int) => "sum=" + (a + b)
  case (s: String, _) => "first=" + s
  case (x, y, z) => "3-tuple"
  case _ => "?"
}

@main def run(): Unit = {
  val (a, b) = (1, 2)
  println(f((1, 2)) + " " + f(("a", 1)) + " swapped=" + (b, a) + " " + f((1, 2, 3)))
}
