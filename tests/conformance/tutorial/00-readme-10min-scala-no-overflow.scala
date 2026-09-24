// EXPECT: 265252859812191058636308480000000
// Point 2.
def factorial(n: Int): Int = if n == 0 then 1 else n * factorial(n - 1)
@main def run(): Unit = println(factorial(30))
