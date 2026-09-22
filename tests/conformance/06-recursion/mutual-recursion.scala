// EXPECT: true
def isEven(n: Int): Boolean = if n == 0 then true else isOdd(n - 1)
def isOdd(n: Int): Boolean = if n == 0 then false else isEven(n - 1)
@main def run(): Unit = println(isEven(10000))
