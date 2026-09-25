// EXPECT: 1 1
// `import Obj.a as b` binds the new name; Scala 2's `a => b` spells the same
// thing and both are accepted. The old name is NOT bound by a rename, so this
// prints the value twice under two names and never mentions `a`.
object Cfg:
  val a = 1
import Cfg.a as z
import Cfg.{a => w}
@main def run(): Unit = println(z.toString + " " + w)
