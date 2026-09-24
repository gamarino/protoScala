// EXPECT: recovered: cannot divide by zero
// A failed future raises AT the await's call site, so an enclosing `try` inside the
// suspended handler catches it and the rest of the handler still runs.
@main def run(): Unit =
  val divider = Actor.spawn(0) { (s, m) =>
    if m == 0 then throw new ArithmeticException("cannot divide by zero") else (s, 100 / m)
  }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = "none"
    try
      out = (divider ? 0).await.toString
    catch
      case e: ArithmeticException => out = "recovered: " + e.getMessage
    (s, out)
  }
  println((caller ? 1).await)
