// EXPECT: 500000500000 1000000
// A local (nested) @tailrec def is a loop in its own frame; it may read a
// local of the enclosing function, which it captures.
def total(n: Long): Long =
  @annotation.tailrec
  def go(i: Long, acc: Long): Long = if i > n then acc else go(i + 1, acc + i)
  go(1L, 0L)

def countDown(n: Int): Int =
  val step = 1
  @annotation.tailrec
  def loop(i: Int, seen: Int): Int = if i == 0 then seen else loop(i - step, seen + step)
  loop(n, 0)

@main def run(): Unit = println(s"${total(1000000L)} ${countDown(1000000)}")
