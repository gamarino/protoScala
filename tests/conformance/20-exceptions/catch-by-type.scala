// EXPECT: arithmetic
// The first matching clause wins, in source order, exactly as a `match` does.
@main def run(): Unit =
  try
    println(1 / 0)
  catch
    case e: IllegalArgumentException => println("illegal")
    case e: ArithmeticException      => println("arithmetic")
    case e: Throwable                => println("other")
