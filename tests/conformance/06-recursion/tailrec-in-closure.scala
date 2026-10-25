// EXPECT-ERROR: Cannot rewrite recursive call: it is not in tail position
// A self-call inside a function literal cannot be a loop (scalac 3.9).
@annotation.tailrec
def f(n: Int): Int =
  val g = () => f(n - 1)
  if n == 0 then 0 else f(n - 1)

@main def run(): Unit = println(f(10))
