// EXPECT: false true side=0
@main def run(): Unit =
  var side = 0
  def touch(): Boolean = { side += 1; true }
  val a = false && touch()
  val b = true || touch()
  println(a.toString + " " + b + " side=" + side)
