// EXPECT: assertion failed: balance went negative
def withdraw(balance: Int, amount: Int): Int =
  require(amount > 0, "amount must be positive")
  val left = balance - amount
  assert(left >= 0, "balance went negative")
  left
@main def run(): Unit =
  println(withdraw(100, 30))
  try withdraw(100, -5) catch case e: IllegalArgumentException => println(e.getMessage)
  try withdraw(100, 200) catch case e: AssertionError => println(e.getMessage)
