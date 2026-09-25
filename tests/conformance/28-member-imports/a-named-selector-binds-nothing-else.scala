// EXPECT-ERROR: Not found: f
// The other half of a selector import: a name the selector list did not mention
// is not bound. Without this, `import Obj.{a}` could quietly behave as a wildcard
// and no fixture would notice.
object Cfg:
  val a = 1
  def f(x: Int): Int = x + 10
import Cfg.{a}
@main def run(): Unit = println(f(1))
