// EXPECT-ERROR: StackOverflowError
def down(n: Int): Int = down(n + 1) + 1
@main def run(): Unit = println(down(0))
