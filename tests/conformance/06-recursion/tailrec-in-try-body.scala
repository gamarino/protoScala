// EXPECT-ERROR: Cannot rewrite recursive call: it is not in tail position
// A self-call in a `try` body is not in tail position (scalac 3.9).
@annotation.tailrec
def down(n: Int): Int =
  try { if n == 0 then 0 else down(n - 1) } catch { case _: Exception => -1 }

@main def run(): Unit = println(down(10))
