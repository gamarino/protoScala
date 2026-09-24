// EXPECT-ERROR: no module found for 'py'
// A single-segment path is never a family prefix:  looks for a
// module called py (plan A0-4).
import py
@main def run(): Unit = println(1)
