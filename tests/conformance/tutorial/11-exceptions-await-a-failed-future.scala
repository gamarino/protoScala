// EXPECT: recovered: cannot count zero
// A failed future raises AT the await's call site, so an enclosing `try` inside the
// suspended handler catches it and the rest of the handler still runs.
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("cannot count zero") }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = "none"
    try
      out = (failer ? 1).await.toString
    catch
      case e: IllegalStateException => out = "recovered: " + e.getMessage
    (s, out)
  }
  println((caller ? 1).await)
