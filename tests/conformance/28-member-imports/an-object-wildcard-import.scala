// EXPECT: 1 2 13
// `import Obj.*` on an object in scope brings in its vals and its defs. scalac
// 3.9.0 prints `wildcard: 1 2 13` for the same object and the same three reads.
object Cfg:
  val a = 1
  val b = 2
  def f(x: Int): Int = x + 10
import Cfg.*
@main def run(): Unit = println(a.toString + " " + b + " " + f(3))
