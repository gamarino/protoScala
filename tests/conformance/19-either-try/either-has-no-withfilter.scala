// EXPECT-ERROR: withFilter is not a member
// D67: Scala's Either.withFilter needs a Left to fall back to, which needs the
// static type. `for (x <- e if p)` over an Either therefore fails loudly.
@main def run(): Unit =
  val r: Either[String, Int] = Right(2)
  val ok = for
    x <- r
    if x > 1
  yield x
  println(ok)
