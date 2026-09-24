// EXPECT: 6
// Phase 2's stackable-traits behaviour is untouched: plain super still starts
// AFTER the defining class.
trait Doubling:
  def put(n: Int): Int = n * 2
trait Incrementing extends Doubling:
  override def put(n: Int): Int = super.put(n + 1)
class Q extends Incrementing
@main def run(): Unit = println(new Q().put(2))
