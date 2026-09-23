// EXPECT-ERROR: cannot override a mutable variable
// `override` is never written on a var, not even to implement an abstract one.
trait T {
  var v: Int
}
class B extends T {
  override var v = 5
}
@main def run(): Unit = println(new B().v)
