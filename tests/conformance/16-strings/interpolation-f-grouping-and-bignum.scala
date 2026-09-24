// EXPECT: 1,234,567 12345678901234567890123
// %,d groups with an ASCII comma (D55, no locale) and %d is exact for a
// promoted LargeInteger (D1); %e/%f/%g would round it (D66).
@main def run(): Unit =
  val n = 1234567
  val big = 12345678901234567890123L
  println(f"$n%,d $big%d")
