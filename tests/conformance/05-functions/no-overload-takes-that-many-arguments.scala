// EXPECT-ERROR: no overload of f takes 3 arguments; the alternatives take 1, 2
// With the alternatives known by arity, a call that matches none of them is a
// compile error naming what is available, rather than the runtime arity error a
// single definition would give (D14).
def f(x: Int) = 1
def f(x: Int, y: Int) = 2

@main def run(): Unit = println(f(1, 2, 3))
