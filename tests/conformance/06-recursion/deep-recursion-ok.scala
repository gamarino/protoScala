// EXPECT: 50005000
def sumTo(n: Int): Int = if n == 0 then 0 else n + sumTo(n - 1)
@main def run(): Unit = println(sumTo(10000))
