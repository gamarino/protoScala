// EXPECT-ERROR: TailRec optimisation not applicable, method count contains no recursive calls
// scalac 3.9 rejects a @tailrec method that never calls itself.
@annotation.tailrec
def count(n: Int, acc: Int): Int = n + acc

@main def run(): Unit = println(count(1, 2))
