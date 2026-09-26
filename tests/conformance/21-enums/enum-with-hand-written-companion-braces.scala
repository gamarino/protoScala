// EXPECT: 2 Spades 0
// The brace-syntax twin of enum-with-hand-written-companion.scala.
enum Suit { case Hearts, Spades }
object Suit { def count: Int = Suit.values.length }

@main def run(): Unit = {
  println(Suit.count.toString + " " + Suit.valueOf("Spades") + " " + Suit.Hearts.ordinal)
}
