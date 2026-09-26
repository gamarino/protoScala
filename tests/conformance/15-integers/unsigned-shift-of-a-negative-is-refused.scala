// EXPECT-ERROR: >>> of a negative integer is not supported
// D15. For a negative operand the answer *is* the width: scalac 3.9 prints
// 2147483644 for `-8 >>> 1` as an `Int` and 9223372036854775804 for the same
// expression as a `Long`. protoScala has neither width, so it refuses rather than
// returning one of the two silently.
@main def run(): Unit = println(-8 >>> 1)
