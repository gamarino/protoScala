// EXPECT: caught: insufficient funds
import st.bank.{Account, InsufficientFunds}

@main def run(): Unit =
  val a = Account.`new`()
  val b = Account.`new`()
  println(a.deposit(100))
  println(a.transfer_to(30, b))
  println(b.balance())
  println(a.report((n: Int) => s"balance is $n"))
  try a.withdraw(1000)
  catch case e: InsufficientFunds => println("caught: " + e.messageText())
