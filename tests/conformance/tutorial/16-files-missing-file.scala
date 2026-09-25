// EXPECT: FileNotFoundException: there-is-no-such-file.txt (No such file or directory)
try
  println(Source.fromFile("there-is-no-such-file.txt").mkString)
catch
  case e: FileNotFoundException => println(s"${e.getClass}: ${e.getMessage}")
