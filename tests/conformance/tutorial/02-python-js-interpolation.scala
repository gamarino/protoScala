// EXPECT: Ada spent 25.0 in all | Ada spent 12.50 each
@main def run(): Unit =
  val name = "Ada"
  val total = 12.5
  println(s"$name spent ${total * 2} in all" + " | " + f"$name spent $total%.2f each")
