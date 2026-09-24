// EXPECT: 1,234,567 ff FF 377 +7  7 true a
@main def run(): Unit =
  val n = 1234567
  val h = 255
  val small = 7
  println(f"$n%,d $h%x $h%X $h%o $small%+d $small% d ${true}%b ${'a'}%c")
