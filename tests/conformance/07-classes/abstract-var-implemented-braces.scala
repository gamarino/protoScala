// EXPECT: 5 7 9
// An abstract `var` is implemented by a `var` without `override`, from a trait,
// from an abstract class, and by a constructor parameter.
trait T {
  var v: Int
}
abstract class A {
  var w: Int
}
class B extends T {
  var v = 5
}
class C extends A {
  var w = 7
}
class D(var v: Int) extends T

@main def run(): Unit = {
  val b = new B
  b.v = 5
  println(b.v.toString + " " + new C().w + " " + new D(9).v)
}
