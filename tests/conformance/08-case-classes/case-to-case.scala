// EXPECT-ERROR: case-to-case inheritance is prohibited
case class A(x: Int)
case class B(y: Int) extends A(y)
