// EXPECT-ERROR: Reassignment to val a
@main def run(): Unit =
  val a = 1
  a = 2
