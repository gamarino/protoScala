// EXPECT: body=5 cleanup=9 caught
// A finally body suspends cooperatively too, and the in-flight exception
// survives the suspension: the cleanup awaits, then the saved exception is
// re-raised and the outer handler sees it. If the handler search sat inside the
// C++ catch block, the FutureYield thrown here would propagate out of a live C++
// handler with an exception still in flight.
@main def run(): Unit =
  val echo = Actor.spawn(0) { (s, m) => (s, m) }
  val caller = Actor.spawn(0) { (s, m) =>
    var log = ""
    try
      try
        log = log + "body=" + (echo ? 5).await + " "
        throw new IllegalStateException("boom")
      finally
        log = log + "cleanup=" + (echo ? 9).await + " "
    catch
      case e: IllegalStateException => log = log + "caught"
    (s, log)
  }
  println((caller ? 1).await)
