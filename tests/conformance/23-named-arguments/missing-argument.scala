// EXPECT-ERROR: is missing argument 'b'
// scalac rejects this at compile time (D81).
def f(a: Int, b: Int): Int = a + b
@main def run(): Unit = println(f(a = 1))
