// EXPECT: 100000 100000
// 100000 try/finally cycles: the operand stack must return to the try's entry
// depth every time, and the retry loop must not grow the native stack.
@main def run(): Unit =
  var cleanups = 0
  var caught = 0
  var i = 0
  while i < 100000 do
    try
      try
        throw new RuntimeException("x")
      finally
        cleanups += 1
    catch
      case e: RuntimeException => caught += 1
    i += 1
  println(cleanups.toString + " " + caught)
