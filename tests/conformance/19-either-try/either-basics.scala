// EXPECT: Right(4) Left(bad) 4 fallback true false
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val r: Either[String, Int] = Right(2)
  val l: Either[String, Int] = Left("bad")
  println(r.map(_ * 2).toString + " " + l.map(_ * 2) + " " + r.map(_ * 2).getOrElse(0) + " " +
    l.getOrElse("fallback") + " " + r.isRight + " " + l.isRight)
