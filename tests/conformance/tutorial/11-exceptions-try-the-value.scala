// EXPECT: Success(42) | recovered | 42 -1
@main def run(): Unit =
  val ok = Try("42".toInt)
  val bad = Try("x".toInt)
  println(ok.toString + " | " + bad.recover(e => -1).map(_ => "recovered").getOrElse("?") +
    " | " + ok.getOrElse(0) + " " + bad.getOrElse(-1))
