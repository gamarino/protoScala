// EXPECT: 3 done
// `type X` and `type X >: L <: U` declare an abstract type member: the bounds are
// parsed and discarded, as every other bound is. An alias declared in a template
// body works too. scalac 3.9 prints `3 done`.
trait Shape
type Abstract
type Bounded >: Null <: Shape
class Holder:
  type T = Int
  def get: T = 3

@main def run(): Unit =
  println(new Holder().get.toString + " done")
