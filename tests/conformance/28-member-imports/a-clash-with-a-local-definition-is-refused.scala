// EXPECT-ERROR: is both imported from Cfg and defined here
// A name this file defines and an import also binds is an error rather than a
// silent shadow, because which one won would depend on the compiler's lookup
// order. Phase 6's rule, now reached through the member-import form as well.
object Cfg:
  val a = 1
import Cfg.*
val a = 2
@main def run(): Unit = println(a)
