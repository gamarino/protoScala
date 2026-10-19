// EXPECT: false true P(1,2) P(1,5) P(7,2) P(1,2) true (1,b,2.5) false
// A case class whose constructor only stores its fields, and a tuple: copy
// derives the new instance from the receiver, which must still be a distinct,
// equal instance of the same class and must leave the receiver unchanged.
case class P(x: Int, y: Int)
@main def run(): Unit =
  val p = P(1, 2)
  val q = p.copy()
  val r = p.copy(y = 5)
  val s = p.copy(7)
  val t = (1, "a", 2.5)
  println(s"${p eq q} ${p == q} $q $r $s $p ${r.isInstanceOf[P]} ${t.copy(_2 = "b")} ${t eq t.copy()}")
