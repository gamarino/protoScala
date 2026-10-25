// EXPECT-ERROR: TailRec optimisation not applicable, method f is neither private nor final so can be overridden
// A subclass could override a public, non-final member of a class, so scalac
// 3.9 rejects @tailrec on it.
class C:
  @annotation.tailrec
  def f(n: Int): Int = if n == 0 then 0 else f(n - 1)

@main def run(): Unit = println(new C().f(3))
