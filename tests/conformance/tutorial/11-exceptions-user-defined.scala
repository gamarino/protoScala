// EXPECT: ConfigError: missing key 'port' 7
class ConfigError(msg: String, val line: Int) extends Exception(msg)
@main def run(): Unit =
  try throw new ConfigError("missing key 'port'", 7)
  catch case e: ConfigError => println(e.toString + " " + e.line)
