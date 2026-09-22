// EXPECT: true true true
trait T0
trait T1 extends T0
trait T2 extends T1
trait T3 extends T2
trait T4 extends T3
trait T5 extends T4
class C0 extends T5
class C1 extends C0
class C2 extends C1
class C3 extends C2
class C4 extends C3
class C5 extends C4
@main def run(): Unit =
  val v: Any = new C5
  println(v.isInstanceOf[T0].toString + " " + v.isInstanceOf[C0] + " " + (v match { case _: T3 => true; case _ => false }))
