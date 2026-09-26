// EXPECT: 2 Spades 0
// Scala has exactly one companion object per enum, so a hand-written
// `object Suit` is the same object the desugarer generates for the cases and for
// `values` / `valueOf` / `fromOrdinal`. protoScala compiled two templates
// against one shared type key and crashed with
// `internal error: unordered_map::at`, which D74 calls a bug; the generated
// companion now absorbs the hand-written one. scalac 3.9 prints `2 Spades 0`.
enum Suit:
  case Hearts, Spades
object Suit:
  def count: Int = Suit.values.length

@main def run(): Unit =
  println(Suit.count.toString + " " + Suit.valueOf("Spades") + " " + Suit.Hearts.ordinal)
