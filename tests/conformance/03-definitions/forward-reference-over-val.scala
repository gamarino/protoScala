// EXPECT-ERROR: forward reference to value y extends over the definition of value x
@main def m() =
  val r =
    def f = y
    val x = f
    val y = 1
    x
  println(r)
