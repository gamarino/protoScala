// EXPECT: 2
// The same bug, seen where it bites: an `unapply` reached through
// `import Holder.Even` has to work as a pattern. Binding the object under
// `Holder.Even` instead of `Even` left `case Even(h)` with an unresolved name,
// and the expression `Even.unapply(4)` failed the same way.
object Holder:
  object Even:
    def unapply(n: Int): Option[Int] = if n % 2 == 0 then Some(n / 2) else None
import Holder.Even
@main def run(): Unit = println(4 match { case Even(h) => h })
