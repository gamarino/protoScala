// EXPECT: 21 22
abstract class IntQueue {
  def put(x: Int): Unit
  def get(): Int
}
class BasicIntQueue extends IntQueue {
  private var last = 0
  def put(x: Int): Unit = last = x
  def get(): Int = last
}
trait Doubling extends IntQueue {
  abstract override def put(x: Int): Unit = super.put(2 * x)
}
trait Incrementing extends IntQueue {
  abstract override def put(x: Int): Unit = super.put(x + 1)
}
class Q1 extends BasicIntQueue with Incrementing with Doubling
class Q2 extends BasicIntQueue with Doubling with Incrementing

@main def run(): Unit = {
  val a = new Q1
  a.put(10)
  val b = new Q2
  b.put(10)
  println(a.get().toString + " " + b.get())
}
