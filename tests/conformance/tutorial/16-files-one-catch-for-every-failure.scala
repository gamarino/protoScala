// EXPECT: caught FileNotFoundException, caught FileNotFoundException
// `IOException` is the one class to catch when you do not care which way it
// failed: a missing file, a directory where a file was expected, no permission
// and a file that is not valid UTF-8 are all IOExceptions.
def read(path: String): String =
  try Source.fromFile(path).mkString
  catch case e: IOException => s"caught ${e.getClass}"

println(s"${read("nothing-here.txt")}, ${read(".")}")
