// EXPECT: Success(3) true recovered
// The block form is the idiomatic one and is what the by-name parameter exists
// for: the whole block runs inside Try's catch.
@main def run(): Unit =
  val ok = Try {
    val a = 1
    val b = 2
    a + b
  }
  val ko = Try {
    val xs = List(1)
    xs(9)
  }
  println(ok.toString + " " + ko.isFailure + " " + ko.recover(e => "recovered").get)
