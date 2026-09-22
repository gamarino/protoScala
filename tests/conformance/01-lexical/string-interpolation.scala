// XFAIL: Hello, Ada!
// Interpolated strings are lexed as structured tokens; evaluation comes later.
@main def run(): Unit =
  val name = "Ada"
  println(s"Hello, $name!")
