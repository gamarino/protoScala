// EXPECT: 1000000 500000500000
// A @tailrec method with several parameter lists loops too: every list is
// rebound, the outer ones included.
@annotation.tailrec
def countWith(step: Int)(n: Int, acc: Int): Int =
  if n == 0 then acc else countWith(step)(n - 1, acc + step)

@annotation.tailrec
def sumDown(acc: Long)(n: Long): Long = if n == 0 then acc else sumDown(acc + n)(n - 1)

@main def run(): Unit = println(s"${countWith(1)(1000000, 0)} ${sumDown(0L)(1000000L)}")
