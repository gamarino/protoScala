// EXPECT-ERROR: A is already defined as class A
// The class half of duplicate-object-is-a-naming-error.scala.
class A { def f = 1 }
class A { def g = 2 }

@main def run(): Unit = println(new A().f)
