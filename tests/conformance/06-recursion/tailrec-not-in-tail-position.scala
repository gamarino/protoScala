// EXPECT-ERROR: Cannot rewrite recursive call: it is not in tail position
// scalac 3.9 rejects a @tailrec method whose recursive call is not in tail
// position, with this message.
@annotation.tailrec
def sumTo(n: Int): Int = if n == 0 then 0 else n + sumTo(n - 1)

@main def run(): Unit = println(sumTo(10))
