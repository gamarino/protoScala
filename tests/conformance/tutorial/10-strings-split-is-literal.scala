// EXPECT: List(a, b, c) List(a, , b) List(abc)
@main def run(): Unit =
  println("a.b.c".split(".").toString + " " + "a,,b".split(",") + " " + "abc".split(","))
