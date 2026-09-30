// EXPECT: first second third
// Source.fromFile streams: getLines() reads the file a line at a time instead of
// loading it whole when the source is opened. The proof is a line appended AFTER
// the first one was read, which only a streaming reader can see.
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/io-streaming.txt"
FileIO.write(path, "first\nsecond\n")
val src = Source.fromFile(path)
val lines = src.getLines()
val a = lines.next()
FileIO.append(path, "third\n")
val rest = lines.toList
src.close()
println(a + " " + rest.mkString(" "))
