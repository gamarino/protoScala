// EXPECT: 9
def square(x: Int): Int = x * x
def applyTo(f: Int => Int, v: Int): Int = f(v)
@main def run(): Unit = println(applyTo(square, 3))
