// EXPECT: Right(5) Left(no)
// map/flatMap are right-biased, so a for-comprehension short-circuits on a Left.
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val a: Either[String, Int] = Right(2)
  val b: Either[String, Int] = Right(3)
  val bad: Either[String, Int] = Left("no")
  val ok = for
    x <- a
    y <- b
  yield x + y
  val ko = for
    x <- bad
    y <- b
  yield x + y
  println(ok.toString + " " + ko)
