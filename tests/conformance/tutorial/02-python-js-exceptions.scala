// EXPECT: cannot parse 'abc' | cleaned up
@main def run(): Unit =
  def parse(s: String): String =
    try
      s.toInt.toString
    catch
      case e: NumberFormatException => "cannot parse '" + s + "'"
    finally
      ()
  var log = ""
  try
    throw new IllegalStateException("x")
  catch
    case e: IllegalStateException => log = "cleaned up"
  println(parse("abc") + " | " + log)
