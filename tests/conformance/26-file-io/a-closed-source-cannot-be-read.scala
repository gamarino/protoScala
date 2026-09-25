// EXPECT: IOException: _data/three.txt (Stream Closed)
// Scala's Source holds an open stream, and reading it after `close()` raises
// java.io.IOException "Stream Closed" (verified against scalac 3.9.0).
// protoScala's source holds no descriptor after `fromFile` returns, so it COULD
// have answered happily -- it does not, because a program that reads a closed
// source has a bug and should be told. The message names the origin, which
// Scala's does not.
val src = Source.fromFile("_data/three.txt")
println(src.getLines().length)
src.close()
try println(src.mkString)
catch case e: IOException => println(e.getClass + ": " + e.getMessage)
