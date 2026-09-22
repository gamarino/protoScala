// EXPECT: 3628800 true
def factorial(n: Int): BigInt =
  def loop(i: Int, acc: BigInt): BigInt =
    if i > n then acc else loop(i + 1, acc * i)
  loop(1, 1)

def isEven(n: Int): Boolean =
  def even(k: Int): Boolean = if k == 0 then true else odd(k - 1)
  def odd(k: Int): Boolean = if k == 0 then false else even(k - 1)
  even(n)

@main def run(): Unit =
  println(factorial(10).toString + " " + isEven(1000))
