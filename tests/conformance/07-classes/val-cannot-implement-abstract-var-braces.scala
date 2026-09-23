// EXPECT-ERROR: needs to be abstract, since def v_= is not defined
// An abstract `var` declares an abstract setter too, which a `val` cannot supply.
trait T {
  var v: Int
}
class B extends T {
  val v = 5
}
@main def run(): Unit = println(new B().v)
