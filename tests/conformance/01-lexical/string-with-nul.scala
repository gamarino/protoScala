// EXPECT: 3 0 true
@main def run(): Unit =
  val s = "a\u0000b"
  println(s.length.toString + " " + s(1).toInt + " " + (s == "a" + 0.toChar + "b"))
