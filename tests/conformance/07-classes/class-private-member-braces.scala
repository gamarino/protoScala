// EXPECT-ERROR: value balance is not a member of Account
class Account(private val balance: Int) {
  def canPay(amount: Int) = amount <= balance
}
@main def run(): Unit = {
  val a = new Account(100)
  println(a.canPay(50))
  println(a.balance)
}
