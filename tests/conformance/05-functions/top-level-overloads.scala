// EXPECT: zero one:1 two:3 List(one:1, one:2)
// `def f(x: Int)` and `def f(x: Int, y: Int)` at the top level used to keep only
// the last definition -- `f(1)` raised "wrong number of arguments for f: expected
// 2, got 1" and the first body was unreachable. Scala dispatches overloads on the
// parameter types; protoScala erases those, so it dispatches on the number of
// parameters, which is the part of a signature erasure leaves behind. scalac 3.9
// prints `zero`, `one:1`, `two:3` and `List(one:1, one:2)` for these calls.
def f(x: Int): String = "one:" + x
def f(x: Int, y: Int): String = "two:" + (x + y)
def f(): String = "zero"

@main def run(): Unit =
  println(f() + " " + f(1) + " " + f(1, 2) + " " + List(1, 2).map(f))
