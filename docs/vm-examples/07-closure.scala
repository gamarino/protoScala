def adder(n: Int): Int => Int = (x: Int) => x + n

def counter(): () => Int =
  var c = 0
  () => { c = c + 1; c }

val add5 = adder(5)
println(add5(10))
val next = counter()
next()
println(next())
