// EXPECT: 1166520745455517696 8589934592 36893488147419103232 0
// D112. scalac 3.9 prints `271601776`, `2`, `2` and `128`: it masks the shift
// count to the operand's width, 5 bits for an `Int` and 6 for a `Long`. The mask
// *is* the width, and protoScala's integers have none (D1, permanent), so `<<` is
// an exact multiplication by a power of two and `>>` an exact arithmetic shift.
// Masking to 5 bits would fix `1 << 33` and break `1L << 65`; masking to 6 would
// do the reverse. There is no third answer to pick, so this fixture pins what
// protoScala does and D112 records what Scala does.
@main def run(): Unit =
  println((0x01030507 << 36).toString + " " + (1 << 33) + " " + (1L << 65) + " " + (256 >> 33))
