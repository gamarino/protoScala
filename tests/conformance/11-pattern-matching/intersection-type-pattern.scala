// EXPECT-ERROR: this type cannot be tested at run time
// D37: an intersection type cannot be tested at run time. scalac 3.9 accepts
// `case v: (A & B)` (and `x.isInstanceOf[A & B]`) and tests both components;
// protoScala rejects it at compile time. The unparenthesised form
// `case v: A & B` is a syntax error in scalac and is rejected here too.
trait A
trait B
class C extends A with B

@main def run(): Unit =
  val x: Any = new C
  x match
    case v: (A & B) => println("both")
    case _          => println("no")
