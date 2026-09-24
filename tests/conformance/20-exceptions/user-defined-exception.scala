// EXPECT: MyError: custom 7
class MyError(msg: String, val code: Int) extends Exception(msg)
@main def run(): Unit =
  try
    throw new MyError("custom", 7)
  catch
    case e: MyError => println(e.toString + " " + e.code)
