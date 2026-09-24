// EXPECT: main survived
// A throwing thread body reports on stderr and joins; it never terminates the
// process.
@main def run(): Unit =
  val t = Thread.start(() => throw new RuntimeException("in thread"))
  t.join()
  println("main survived")
