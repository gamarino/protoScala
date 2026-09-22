// EXPECT: 48 1 194 b a 96 97 3 194 48 -98 -97 97 98
@main def run(): Unit =
  val c = 'a'
  println((c / 2).toString + " " + (c % 2) + " " + (c * 2) + " " + (c max 'b') + " " +
    (c min 'b') + " " + (c & 0x60) + " " + (c | 1) + " " + (c ^ 'b') + " " + (c << 1) + " " +
    (c >> 1) + " " + (~c) + " " + (-c) + " " + (+c) + " " + (97 max 'b'))
