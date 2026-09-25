// EXPECT-ERROR: ImportError: no module found for 'c'
// A member import needs a prefix whose class the compiler knows, so that a
// wildcard can enumerate it: an object, a companion or an enum. A `val` has no
// static type here (D4), so it is not one, and the import falls through to the
// module loader and its message rather than guessing a member set.
class C(val x: Int)
val c = new C(1)
import c.*
@main def run(): Unit = println(x)
