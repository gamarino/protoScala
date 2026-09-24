// EXPECT: (1,2)
// README, "protoScala in 10 minutes -- for Python and JavaScript developers",
// point 1: an update returns a NEW map and the old one is unchanged.
@main def run(): Unit =
  val stock = Map("a" -> 1)
  val more = stock + ("b" -> 2)
  println((stock.size, more.size))
