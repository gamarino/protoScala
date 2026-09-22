// EXPECT: 7
@main def run(): Unit = println(helper(3))
def helper(x: Int): Int = x * 2 + 1
