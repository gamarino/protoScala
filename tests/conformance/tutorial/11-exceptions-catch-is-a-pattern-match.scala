// EXPECT: not found: /etc/nope
case class HttpError(code: Int, path: String) extends Exception(path)
@main def run(): Unit =
  val out =
    try throw HttpError(404, "/etc/nope")
    catch
      case HttpError(404, p)          => "not found: " + p
      case HttpError(c, _) if c >= 500 => "server error " + c
      case e: Exception                => "other: " + e.getMessage
  println(out)
