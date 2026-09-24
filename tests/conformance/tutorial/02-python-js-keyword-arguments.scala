// EXPECT: w=2 h=3 d=1
// Python's keyword arguments, and what a JavaScript options object is for.
def box(width: Int, height: Int, depth: Int = 1): String =
  "w=" + width + " h=" + height + " d=" + depth
@main def run(): Unit = println(box(height = 3, width = 2))
