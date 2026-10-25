def sumTo(n: Int): Int =
  var i = 0
  var acc = 0
  while i <= n do
    acc = acc + i
    i = i + 1
  acc

@annotation.tailrec
def count(n: Int, acc: Int): Int =
  if n == 0 then acc else count(n - 1, acc + 1)

println(sumTo(100))
println(count(1000000, 0))
