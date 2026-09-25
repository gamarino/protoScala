// EXPECT: 1 2
// `import Obj.{a, b}` binds exactly those two names and NOT the object's other
// members: `f` stays unbound, which is what the EXPECT-ERROR fixture beside this
// one pins.
object Cfg:
  val a = 1
  val b = 2
  def f(x: Int): Int = x + 10
import Cfg.{a, b}
@main def run(): Unit = println(a.toString + " " + b)
