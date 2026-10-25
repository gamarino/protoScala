// EXPECT: 10 9
// A tail self-call that omits arguments takes them from their defaults, after
// the explicit ones; a default reads the NEW values of the parameters before
// it, as the default getter of a real call would (scalac 3.9 prints the same).
@annotation.tailrec
def dflt(n: Int, acc: Int = 0, step: Int = 1): Int =
  if n == 0 then acc else dflt(n - 1, step = step + 1, acc = acc + step)

@annotation.tailrec
def dflt2(n: Int, acc: Int = 7): Int =
  if n == 0 then acc else if n == 3 then dflt2(n - 1) else dflt2(n - 1, acc + 1)

@main def run(): Unit = println(s"${dflt(4)} ${dflt2(5)}")
