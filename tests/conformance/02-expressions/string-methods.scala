// EXPECT: 12 HELLO, WORLD Hello W 7 true ababab x true 42
@main def run(): Unit =
  val s = "Hello, World"
  println(s.length.toString + " " + s.toUpperCase + " " + s.substring(0, 5) + " " + s(7) +
    " " + s.indexOf("World") + " " + s.contains("lo") + " " + ("ab" * 3) + " " + "  x ".trim +
    " " + s.startsWith("Hell") + " " + "42".toInt)
