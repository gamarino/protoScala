// EXPECT: 12 104 302 4
// CALL_KW: `f(x = 1)` on a function value or a local def, which SEND_KW cannot
// express because it has no receiver.
//
// D89: scalac REJECTS a named argument on a function value — `Function2.apply`'s
// parameters are called `v1` and `v2`, so `f(b = 1, a = 2)` is "method apply in
// trait Function2 does not have a parameter a". protoScala binds against the
// names written in the function literal, which is what a reader expects and what
// falls out of binding in the callee: the callee IS the literal. Permissiveness,
// not a semantic mismatch — no program scalac accepts behaves differently here.
object Holder:
  val fn: (Int, Int) => Int = (a, b) => a - b
@main def run(): Unit =
  val f = (a: Int, b: Int) => a + b * 10
  def g(a: Int, b: Int = 4): Int = a * 100 + b
  println(f(b = 1, a = 2).toString + " " + g(a = 1) + " " + g(b = 2, a = 3) + " " +
    Holder.fn(b = 1, a = 5))
