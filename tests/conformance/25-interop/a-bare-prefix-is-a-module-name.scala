// EXPECT-ERROR: no module found for 'py'
// One segment is never a prefix (the rule needs at least two), so this looks for
// a module called py on the resolution chain. The edge a reader asks about.
import py
@main def run(): Unit = println(1)
