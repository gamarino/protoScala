def safeDiv(a: Int, b: Int): Int =
  try
    if b == 0 then throw new ArithmeticException("divide by zero")
    a / b
  catch
    case e: ArithmeticException => -1
  finally
    println("done")

println(safeDiv(6, 3))
println(safeDiv(1, 0))
