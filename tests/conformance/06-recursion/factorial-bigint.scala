// EXPECT: 265252859812191058636308480000000
def fact(n: Int): BigInt = if n <= 1 then 1 else n * fact(n - 1)
@main def run(): Unit = println(fact(30))
