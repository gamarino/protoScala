// EXPECT: 1a 1b 2a 2b
@main def run(): Unit =
  var out = ""
  for x <- List(1, 2); y <- List("a", "b") do out = out + (if out.isEmpty then "" else " ") + x + y
  println(out)
