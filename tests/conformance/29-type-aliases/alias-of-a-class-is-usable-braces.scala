// EXPECT: 7 true true int
// The brace-syntax twin of alias-of-a-class-is-usable.scala.
class Box(val v: Int)
trait Shape
type B = Box
type S = Shape
class Square extends S

@main def run(): Unit = {
  val b: B = new B(7)
  val z: Any = 5
  val tag = z match { case s: String => "string"; case i: Int => "int" }
  println(b.v.toString + " " + b.isInstanceOf[B] + " " + (new Square).isInstanceOf[S] + " " + tag)
}
