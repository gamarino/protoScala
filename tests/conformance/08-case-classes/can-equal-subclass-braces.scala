// EXPECT: true true true false true true false true false
case class A(x: Int)
class B(x: Int) extends A(x)
@main def run(): Unit = {
  val b = new B(1)
  println((A(1) == b).toString + " " + (b == A(1)) + " " + (b == new B(1)) + " " + (A(1) == A(2)) +
    " " + A(1).canEqual(b) + " " + b.canEqual(A(1)) + " " + A(1).canEqual(1) + " " + (1, 2).canEqual((1, 2)) +
    " " + (1, 2).canEqual((1, 2, 3)))
}
