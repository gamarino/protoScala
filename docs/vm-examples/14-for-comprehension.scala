val pairs = for
  x <- List(1, 2, 3)
  if x != 2
  y <- List(10, 20)
yield x * y

println(pairs)
