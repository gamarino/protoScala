// EXPECT-ERROR: no provider registered for 'py'
// Only segment 0 is stripped: the provider is asked for `a.b`, not for `py.a.b`.
import py.a.b
@main def run(): Unit = println(1)
