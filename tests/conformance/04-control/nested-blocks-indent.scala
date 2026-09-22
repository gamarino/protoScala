// EXPECT: 6
@main def run(): Unit =
  val x =
    val a = 1
    val b =
      val c = 2
      c * 2
    a + b + 1
  println(x)
