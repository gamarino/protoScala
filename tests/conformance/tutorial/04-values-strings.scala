// EXPECT: HELLO 5 ell hello!!! true
@main def run(): Unit =
  val s = "hello"
  println(s.toUpperCase + " " + s.length + " " + s.substring(1, 4) + " " + s + "!" * 3 + " " + s.startsWith("he"))
