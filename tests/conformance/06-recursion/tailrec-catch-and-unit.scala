// EXPECT: 3 done 100000
// The `catch` cases of a `try` without `finally` are in tail position, and a
// Unit method's self-call is too, although its body discards a value.
@annotation.tailrec
def retry(n: Int, failures: Int): Int =
  try
    if n > 0 then throw new RuntimeException("again") else failures
  catch
    case _: RuntimeException => retry(n - 1, failures + 1)

var ticks = 0

@annotation.tailrec
def tick(n: Int): Unit =
  if n > 0 then
    ticks += 1
    tick(n - 1)

@main def run(): Unit =
  tick(100000)
  println(s"${retry(3, 0)} done $ticks")
