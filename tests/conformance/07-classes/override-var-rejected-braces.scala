// EXPECT-ERROR: cannot override a mutable variable
class A {
  var v = 1
}
class B extends A {
  override var v = 2
}
@main def run(): Unit = println(new B().v)
