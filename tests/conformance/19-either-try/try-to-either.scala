// EXPECT: Right(7) true
@main def run(): Unit =
  val ok = Try(7)
  val ko = Try(None.get)
  println(ok.toEither.toString + " " + ko.toEither.isLeft)
