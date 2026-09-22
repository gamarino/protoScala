// EXPECT: A B T1 T2 C
var log = ""
def note(s: String): Unit = log = if log.isEmpty then s else log + " " + s
class A:
  note("A")
trait T1 extends A:
  note("T1")
trait T2 extends A:
  note("T2")
class B extends A:
  note("B")
class C extends B with T1 with T2:
  note("C")

@main def run(): Unit =
  new C
  println(log)
