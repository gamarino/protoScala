// EXPECT: ok UnsupportedOperationException
// `Source.fromFile(path, enc)` exists so the common Scala spelling compiles.
// protoScala decodes UTF-8 and nothing else (D99). A charset the JVM supports and
// this runtime does not is REFUSED rather than decoded as if it had been UTF-8:
// silently mis-decoding ISO-8859-1 would hand the program plausible nonsense.
val ok = Source.fromFile("_data/utf8.txt", "utf-8").getLines()(0).length
try
  Source.fromFile("_data/utf8.txt", "ISO-8859-1").mkString
  println("FAIL: ISO-8859-1 was accepted")
catch case e: UnsupportedOperationException => println("ok " + e.getClass)
