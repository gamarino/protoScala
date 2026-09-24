// EXPECT: Hello, Ada!
// Interpolated strings are lexed as structured tokens and lowered to CONCAT
// (Phase 3, A0-1, D54).
@main def run(): Unit =
  val name = "Ada"
  println(s"Hello, $name!")
