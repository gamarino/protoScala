// EXPECT: 2 miss
class Money(val cents: Int):
  override def equals(other: Any): Boolean =
    other.isInstanceOf[Money] && other.asInstanceOf[Money].cents == cents

@main def run(): Unit =
  val prices = Map(new Money(100) -> "a coffee", new Money(100) -> "a tea")
  println(prices.size.toString + " " + prices.getOrElse(new Money(100), "miss"))
