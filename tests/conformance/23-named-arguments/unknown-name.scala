// EXPECT-ERROR: has no parameter named 'z'
// D81: because binding happens in the CALLEE, a typo in a parameter name is a
// run-time error here and a compile error in scalac. The platform is late-binding
// even where Scala is not; late detection is accepted, silent failure is not, so
// the message names what was expected and what arrived.
def f(a: Int, b: Int): Int = a + b
@main def run(): Unit = println(f(a = 1, z = 2))
