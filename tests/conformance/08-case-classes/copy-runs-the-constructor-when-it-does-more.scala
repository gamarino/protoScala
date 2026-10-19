// EXPECT: 2 8 W(4.0) base:3 1 true
// copy goes through the primary constructor whenever it does more than store
// its parameters: a body statement, a body val, a Double parameter's widening,
// a superclass initialiser, and mutable (var) instances.
object Log { var runs = 0 }
case class C(x: Int) { Log.runs += 1 }
case class D(x: Int) { val twice = x * 2 }
case class W(d: Double)
class Base(val n: Int) { val tag = "base:" + n }
case class E(x: Int) extends Base(x)
case class M(var x: Int)
@main def run(): Unit =
  C(1).copy(x = 2)
  val m = M(1)
  val m2 = m.copy()
  m2.x = 5
  println(s"${Log.runs} ${D(3).copy(x = 4).twice} ${W(1.5).copy(d = 4)} ${E(1).copy(x = 3).tag} ${m.x} ${!(m eq m2)}")
