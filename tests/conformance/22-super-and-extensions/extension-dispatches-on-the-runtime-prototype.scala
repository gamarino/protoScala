// EXPECT: 6 6
// D6: an extension dispatches on the RUNTIME prototype, not the static type, so a
// value reached through Any answers it too.
extension (n: Int) def triple: Int = n * 3
@main def run(): Unit =
  val any: Any = 2
  println(2.triple.toString + " " + any.asInstanceOf[Int].triple)
