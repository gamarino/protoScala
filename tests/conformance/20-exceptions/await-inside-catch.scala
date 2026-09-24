// EXPECT: caught x then 7
// A catch body is ORDINARY BYTECODE, so it suspends cooperatively like any other
// code — which is only true because the handler is entered outside the C++ catch
// block. The bound `e` survives the suspension because it lives in a local slot
// that the frame snapshot saves.
@main def run(): Unit =
  val echo = Actor.spawn(0) { (s, m) => (s, m) }
  val caller = Actor.spawn(0) { (s, m) =>
    var out = "none"
    try
      throw new RuntimeException("x")
    catch
      case e: RuntimeException =>
        val v = (echo ? 7).await
        out = "caught " + e.getMessage + " then " + v
    (s, out)
  }
  println((caller ? 1).await)
