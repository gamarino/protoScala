// EXPECT: 5 5
@main def run(): Unit = {
  var caught = 0
  var ok = 0
  var i = 0
  while (i < 10) {
    try {
      if (i % 2 == 0) throw new RuntimeException("x") else ok += 1
    } catch {
      case e: RuntimeException => caught += 1
    }
    i += 1
  }
  println(caught.toString + " " + ok)
}
