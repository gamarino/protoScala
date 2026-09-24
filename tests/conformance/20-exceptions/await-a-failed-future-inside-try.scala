// EXPECT: recovered boom
// A failed future raises AT the await's call site, so an enclosing try catches
// it and the rest of the handler still runs. This is what retires D50.
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("boom") }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = "none"
    try
      out = (failer ? 1).await.toString
    catch
      case e: IllegalStateException => out = "recovered " + e.getMessage
    (s, out)
  }
  println((caller ? 1).await)
