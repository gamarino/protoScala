// EXPECT: 7 O.C
object O:
  class C(val n: Int):
    override def toString: String = "O.C"
@main def run(): Unit =
  val c = new O.C(7)
  println(c.n.toString + " " + c)
