// EXPECT: 7 true true int
// An alias of a class or trait works wherever the class name would: as the type
// of a `new`, as a parent, in an `isInstanceOf` and in a type pattern. scalac 3.9
// prints `7 true true int`.
class Box(val v: Int)
trait Shape
type B = Box
type S = Shape
class Square extends S

@main def run(): Unit =
  val b: B = new B(7)
  val z: Any = 5
  val tag = z match
    case s: String => "string"
    case i: Int    => "int"
  println(b.v.toString + " " + b.isInstanceOf[B] + " " + (new Square).isInstanceOf[S] + " " + tag)
