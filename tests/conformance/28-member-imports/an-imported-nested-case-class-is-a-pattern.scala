// EXPECT: 3
// A nested case class imported by name brings its TYPE and its companion, so the
// constructor application and the constructor pattern both compile — the property
// Phase 6 pinned for a module's nested type, now through the member-import form.
object Holder:
  case class Point(x: Int, y: Int)
import Holder.Point
@main def run(): Unit =
  val p = Point(1, 2)
  println(p match { case Point(a, b) => a + b })
