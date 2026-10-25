// EXPECT: 500000500000 300000 7 400000 250000
// @tailrec on members: an object's method, a private and a final method of a
// class (called bare and through `this`), and a final method of a trait. The
// receiver stays in slot 0 across iterations.
object Sums:
  @annotation.tailrec
  def upTo(n: Long, acc: Long): Long = if n == 0 then acc else upTo(n - 1, acc + n)

class Counter(val step: Int):
  @annotation.tailrec
  private def loop(n: Int, acc: Int): Int = if n == 0 then acc else loop(n - 1, acc + step)
  def run(n: Int): Int = loop(n, 0)
  @annotation.tailrec
  final def viaThis(n: Int, acc: Int): Int = if n <= 0 then acc + step else this.viaThis(n - 1, acc)

trait Steps:
  @annotation.tailrec
  final def steps(n: Int, acc: Int): Int = if n == 0 then acc else steps(n - 1, acc + 1)

final class Doubler extends Steps:
  @annotation.tailrec
  def twice(n: Int, acc: Int): Int = if n == 0 then acc else twice(n - 1, acc + 2)

@main def run(): Unit =
  val c = new Counter(3)
  val d = new Doubler()
  println(s"${Sums.upTo(1000000L, 0L)} ${c.run(100000)} ${c.viaThis(1000000, 4)} ${d.steps(400000, 0)} ${d.twice(125000, 0)}")
