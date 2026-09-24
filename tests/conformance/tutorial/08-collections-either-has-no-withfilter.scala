// EXPECT-ERROR: withFilter is not a member of Right
@main def run(): Unit =
  val r: Either[String, Int] = Right(2)
  val kept = for
    x <- r
    if x > 1
  yield x
  println(kept)
