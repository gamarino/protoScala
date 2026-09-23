// EXPECT: 1
// D53: a call the compiler cannot resolve to a declaration — here a method
// reached through a receiver whose class is not known at the call site — has no
// signature, so the argument is evaluated. scalac 3.9 prints only `1`;
// protoScala prints "eager" first. The deviation is visible, not silent.
class Eager:
  def first(a: => Int, b: => Int): Int = a
val e = new Eager()
println(e.first(1, { println("eager"); 2 }))
