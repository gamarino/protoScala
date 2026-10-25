// EXPECT: 10 3 4 2.5 100000
// A tail self-call binds its arguments as a call does: named arguments in any
// order, a by-name argument as a thunk, a repeated parameter from a splice or
// from plain arguments, and Double widening of an Int argument.
@annotation.tailrec
def named(n: Int, acc: Int, step: Int): Int =
  if n == 0 then acc else named(n - 1, step = step + 1, acc = acc + step)

@annotation.tailrec
def byName(n: Int, x: => Int): Int = if n == 0 then x else byName(n - 1, x + 1)

@annotation.tailrec
def countArgs(acc: Int, xs: Int*): Int =
  if xs.isEmpty then acc
  else if xs.head == 0 then countArgs(acc + 1, 1, 2, 3)
  else countArgs(acc + 1, xs.tail*)

@annotation.tailrec
def halve(x: Double, n: Int): Double = if n == 0 then x else halve(5, n - 1)

@annotation.tailrec
def spin(n: Int, more: Int*): Int = if n == 0 then more.length else spin(n - 1, more*)

@annotation.tailrec
def sevens(n: Int, acc: List[Int]): List[Int] = if n == 0 then acc else sevens(n - 1, 7 :: acc)

@main def run(): Unit =
  val xs = sevens(100000, Nil)
  println(s"${named(4, 0, 1)} ${byName(3, 0)} ${countArgs(0, 0)} ${halve(1, 3) / 2} ${spin(5, xs*)}")
