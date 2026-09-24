// EXPECT-ERROR: has no parameter named 'depth'
// D81: because a named argument is bound in the CALLEE, a typo in a parameter name
// is a run-time error here and a compile error in scalac. The message names the
// method and the parameter, so late detection never becomes silent failure.
def area(width: Int, height: Int): Int = width * height
@main def run(): Unit = println(area(width = 2, depth = 3))
