// EXPECT-ERROR: value b is not a member of A
class A:
  val a = b + 1
  val b = 1

@main def run(): Unit = println(new A().a)
