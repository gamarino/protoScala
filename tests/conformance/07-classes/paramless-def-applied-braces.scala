// EXPECT-ERROR: method p does not take parameters
class H {
  def p = 1
  def q() = 2
}
@main def run(): Unit = {
  println(new H().q())
  println(new H().p())
}
