// EXPECT: err:bad ok:2 Left(2) Right(bad)
// Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val r: Either[String, Int] = Right(2)
  val l: Either[String, Int] = Left("bad")
  println(l.fold(s => "err:" + s, n => "ok:" + n) + " " +
    r.fold(s => "err:" + s, n => "ok:" + n) + " " + r.swap + " " + l.swap)
