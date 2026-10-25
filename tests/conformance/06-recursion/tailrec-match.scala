// EXPECT: 20000100000 200000
// Self-calls in `match` case bodies are in tail position; so is the right
// operand of `&&` and `||`, and the value of a `return`.
@scala.annotation.tailrec
def build(n: Int, acc: List[Long]): List[Long] =
  if n == 0 then acc else build(n - 1, n.toLong :: acc)

@scala.annotation.tailrec
def sum(xs: List[Long], acc: Long): Long = xs match
  case Nil => acc
  case h :: t => sum(t, acc + h)

@annotation.tailrec
def allPositive(xs: List[Long]): Boolean = xs.isEmpty || (xs.head > 0 && allPositive(xs.tail))

@annotation.tailrec
def lengthOf(xs: List[Long], n: Int): Int =
  if xs.isEmpty then return n
  return lengthOf(xs.tail, n + 1)

@main def run(): Unit =
  val xs = build(200000, Nil)
  println(allPositive(xs))
  println(s"${sum(xs, 0L)} ${lengthOf(xs, 0)}")
