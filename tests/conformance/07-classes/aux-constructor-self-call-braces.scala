// EXPECT-ERROR: must call a preceding constructor
class A(x: Int) {
  def this(y: Int, z: Int) = this(y, z)
}
@main def run(): Unit = println(new A(1, 2).toString)
