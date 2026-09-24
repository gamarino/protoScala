// EXPECT: Success(2) true -1 Success(20) Success(0) true
@main def run(): Unit =
  val ok = Try {
    List(1, 2, 3)(1)
  }
  val bad = Try {
    List(1, 2, 3)(9)
  }
  println(ok.toString + " " + bad.isFailure + " " + bad.getOrElse(-1) + " " +
    ok.map(_ * 10) + " " + bad.recover(e => 0) + " " + bad.toEither.isLeft)
