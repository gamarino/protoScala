// EXPECT-ERROR: needs to be a stable, immutable value
class A {
  val v = 1
}
class B extends A {
  override def v = 2
}
@main def run(): Unit = println(new B().v)
