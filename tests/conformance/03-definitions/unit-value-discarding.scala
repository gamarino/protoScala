// EXPECT: () () () () () () ()
def u(): Unit = 5
def early(n: Int): Unit =
  if n > 0 then return n
  n * 2
def curried(a: Int)(b: Int): Unit = a + b
def braces(): Unit = { val k = 1; k + 1 }
@main def run(): Unit =
  val v: Unit = 5
  val w = (7: Unit)
  println(u().toString + " " + early(1) + " " + early(-1) + " " + curried(1)(2) + " " +
    braces() + " " + v + " " + w)
