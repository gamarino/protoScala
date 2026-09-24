// EXPECT: Right(42) Left(not a number: x) Right(43) Left(not a number: x) 0 err not a number: x None
def parse(s: String): Either[String, Int] =
  if s == "42" then Right(42) else Left("not a number: " + s)

@main def run(): Unit =
  println(parse("42").toString + " " + parse("x") + " " + parse("42").map(_ + 1) + " " +
    parse("x").map(_ + 1) + " " + parse("x").getOrElse(0) + " " +
    parse("x").fold(e => "err " + e, n => "ok " + n) + " " + parse("x").toOption)
