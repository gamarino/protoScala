// EXPECT: 3
// README, "protoScala in 10 minutes -- for Scala programmers", point 1.
def add(a: Int, b: Int): String = a + b
@main def run(): Unit = println(add(1, 2))
