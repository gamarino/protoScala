// EXPECT-ERROR: needs an `override` modifier
class A(val v: Int)
class B(val v: Int) extends A(0)
@main def run(): Unit = println(new B(5).v)
