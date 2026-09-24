// EXPECT: Success(2) true 2 Success(4) Success(-1) 99
// `Try { e }` and `Try(e)` take the body by-name (A0-12; by-name parameters
// landed on main in bca0352), so the failure is caught inside Try, not at the
// call site. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val ok = Try(2)
  val ko = Try("x".toInt)
  println(ok.toString + " " + ko.isFailure + " " + ok.get + " " + ok.map(_ * 2) + " " +
    ko.map(_ * 2).recover(e => -1) + " " + ko.getOrElse(99))
