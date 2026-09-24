// EXPECT: message was boom
// A catch clause is a full pattern match: a constructor pattern, a guard and a
// type pattern in one handler. This is the part Scala programmers under-use and
// Python's `except` has no analogue for.
case class AppError(code: Int, detail: String) extends Exception(detail)
@main def run(): Unit =
  try
    throw AppError(7, "boom")
  catch
    case AppError(7, d)            => println("message was " + d)
    case AppError(c, _) if c > 100 => println("big " + c)
    case e: Exception              => println("plain")
