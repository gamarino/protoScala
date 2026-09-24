// EXPECT: cleanup recovered
// A failed await raises at its call site, so the enclosing finally runs and then
// the enclosing catch sees the exception — the ordinary order, across a
// suspension.
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("boom") }
  val caller = Actor.spawn(0) { (s, m) =>
    var log = ""
    try
      try
        (failer ? 1).await
      finally
        log = log + "cleanup "
    catch
      case e: IllegalStateException => log = log + "recovered"
    (s, log)
  }
  println((caller ? 1).await)
