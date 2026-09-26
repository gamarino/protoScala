// EXPECT: 3 done
// The brace-syntax twin of abstract-and-member-aliases.scala.
trait Shape
type Abstract
type Bounded >: Null <: Shape
class Holder { type T = Int; def get: T = 3 }

@main def run(): Unit = {
  println(new Holder().get.toString + " done")
}
