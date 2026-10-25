// EXPECT: 1000000
// A @tailrec method's self-call in tail position is a loop (JUMP_BACK), so a
// million iterations need no native stack (scalac 3.9 prints the same).
@annotation.tailrec
def count(n: Int, acc: Int): Int =
  if n == 0 then acc else count(n - 1, acc + 1)

@main def run(): Unit = println(count(1000000, 0))
