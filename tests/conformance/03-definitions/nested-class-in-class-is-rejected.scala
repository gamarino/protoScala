// EXPECT-ERROR: must be defined at the top level of a file or in an object
// A template nested in a CLASS would capture the enclosing instance, which needs
// a per-instance class; that is deliberately out of scope (D80), along with local
// classes and the anonymous-class form `new T { ... }`.
class Outer:
  class Inner(val n: Int)
@main def run(): Unit = println(1)
