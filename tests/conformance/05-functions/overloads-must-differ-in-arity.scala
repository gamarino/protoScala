// EXPECT-ERROR: overloads must differ in their number of parameters
// Two alternatives of the same arity need the parameter types to be told apart,
// and protoScala has none (D111). scalac accepts this program; protoScala reports
// it rather than silently keeping the second body, which is what it used to do.
def f(x: Int) = 1
def f(x: String) = 2

@main def run(): Unit = println(f(1))
