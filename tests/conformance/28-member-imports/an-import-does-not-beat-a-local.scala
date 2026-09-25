// EXPECT: 99
// An imported member is consulted after locals, so an inner scope wins — the
// rule Phase 6 recorded for module imports, and it has to hold for this form too.
object Cfg:
  val a = 1
import Cfg.*
@main def run(): Unit =
  val a = 99
  println(a)
