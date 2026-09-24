// EXPECT: message was boom
case class AppError(code: Int, detail: String) extends Exception(detail)
@main def run(): Unit = {
  try {
    throw AppError(7, "boom")
  } catch {
    case AppError(7, d)            => println("message was " + d)
    case AppError(c, _) if c > 100 => println("big " + c)
    case e: Exception              => println("plain")
  }
}
