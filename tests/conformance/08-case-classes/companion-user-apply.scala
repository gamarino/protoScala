// EXPECT: Money(250) Money(3)
case class Money(cents: Int)
object Money:
  def apply(s: String): Money = new Money((s.toDouble * 100).round.toInt)

@main def run(): Unit =
  println(Money("2.50").toString + " " + new Money(3))
