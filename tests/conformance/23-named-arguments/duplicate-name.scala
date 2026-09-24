// EXPECT-ERROR: received parameter 'a' twice
// scalac rejects this at compile time (D81).
def f(a: Int, b: Int): Int = a + b
@main def run(): Unit = println(f(1, a = 2))
