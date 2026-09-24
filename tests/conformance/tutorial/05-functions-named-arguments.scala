// EXPECT: 3 13 3 | b a
def volume(width: Int, height: Int = 1, depth: Int = 1): Int = width * height * depth
var log = ""
def side(tag: String, v: Int): Int =
  log = log + tag + " "
  v
@main def run(): Unit =
  val reordered = volume(depth = 3, width = 1)
  val order = volume(height = side("b", 1), width = side("a", 3))
  println(volume(3).toString + " " + volume(1, 13) + " " + reordered + " | " + log.trim)
