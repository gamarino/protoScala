// EXPECT: 1 3 4 10
def m(): String =
  def f = z + w
  lazy val z = 1
  lazy val w = z + 1
  val shown = z.toString + " " + f
  shown
def n(): Int =
  var total = 0
  total = z + 1
  lazy val z = 3
  total
def even(k: Int): Boolean = if k == 0 then true else odd(k - 1)
def odd(k: Int): Boolean = if k == 0 then false else even(k - 1)
def local(): Int =
  def isEven(k: Int): Boolean = if k == 0 then true else isOdd(k - 1)
  def isOdd(k: Int): Boolean = if k == 0 then false else isEven(k - 1)
  if isEven(10) && even(4) then 10 else 0
println(m() + " " + n() + " " + local())
