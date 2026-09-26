// EXPECT-ERROR: cannot have a default parameter value
// A default value turns an alternative's acceptable argument count into a range,
// so the count no longer identifies it. scalac 3.9 accepts this program (it
// prints 1) because it resolves the call by type; protoScala resolves it by
// argument count and refuses rather than guessing (D111). A single `def` with a
// default parameter is unaffected.
def f(x: Int) = 1
def f(x: Int, y: Int = 2) = 3

@main def run(): Unit = println(f(1))
