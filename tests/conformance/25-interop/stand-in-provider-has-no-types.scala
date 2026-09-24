// EXPECT-ERROR: Not found: type probe
// D94: a foreign module binds no types, so `new` on one of its members is not
// available. The error is late, which is what a late-binding platform means --
// but it is loud.
import js.probe as p
@main def run(): Unit = println(new probe())
