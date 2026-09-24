// EXPECT: Leaf(1) 1
// Inside the object it was written in, a lifted template resolves without the
// qualifier, which is Scala's scoping.
object Ast2:
  sealed trait T
  case class Leaf(n: Int) extends T
  def mk(n: Int): T = Leaf(n)
  def value(t: T): Int = t match
    case Leaf(n) => n
@main def run(): Unit =
  val t = Ast2.mk(1)
  println(t.toString + " " + Ast2.value(t))
