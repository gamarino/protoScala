// EXPECT-ERROR: Reassignment to val x
class P(val x: Int)
@main def run(): Unit =
  val p = new P(1)
  p.x = 2
