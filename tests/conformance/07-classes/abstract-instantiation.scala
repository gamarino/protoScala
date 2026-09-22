// EXPECT-ERROR: A is abstract; it cannot be instantiated
abstract class A
@main def run(): Unit = println(new A)
