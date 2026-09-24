// EXPECT: Success(0) Success(1) Success(2) true
@main def run(): Unit =
  val ko = Try(None.get)
  val ok = Try(2)
  println(ko.recoverWith(e => Try(0)).toString + " " + ko.orElse(Try(1)) + " " +
    ok.orElse(Try(1)) + " " + ko.flatMap(x => Try(x)).isFailure)
