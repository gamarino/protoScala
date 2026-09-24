// EXPECT: 30
// A cooperative suspension is not an exception: the try must not catch it, and
// the finally must not run while the frame is suspended. FutureYield is not a
// std::exception and runLoop's catch for it is first, so it passes straight
// through the retry loop untouched.
@main def run(): Unit =
  val echo = Actor.spawn(0) { (s, m) => (s, m) }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = 0
    try
      out = (echo ? 10).await + (echo ? 20).await
    catch
      case e: Throwable => out = -1
    (s, out)
  }
  val f = caller ? 1
  println(f.await)
