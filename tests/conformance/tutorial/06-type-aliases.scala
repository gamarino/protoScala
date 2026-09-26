// EXPECT: 3 7 true
// Tutorial chapter 6, §6.10. Verified against scalac 3.9.0.
type Name = String
class Box(val v: Int)
type B = Box
trait Shape
type S = Shape
class Square extends S

@main def run(): Unit =
  val n: Name = "abc"
  val b: B = new B(7)
  println(n.length.toString + " " + b.v + " " + (new Square).isInstanceOf[S])
