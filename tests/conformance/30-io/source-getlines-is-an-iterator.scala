// EXPECT: <iterator> 3 0 true
// getLines() answers an Iterator, as in Scala: it prints as <iterator>, and it is
// consumed as it is read, so a second pass over the same source finds nothing
// (verified against scalac 3.9.0).
val tmp = System.getenv("PROTOSCALA_TEST_TMP")
if tmp == "" then throw new IllegalStateException("PROTOSCALA_TEST_TMP is not set")
val path = tmp + "/io-iterator.txt"
FileIO.write(path, "a\nb\nc\n")
val src = Source.fromFile(path)
val it = src.getLines()
val shown = it.toString
val n = it.length
println(shown + " " + n + " " + src.getLines().length + " " + it.isEmpty)
