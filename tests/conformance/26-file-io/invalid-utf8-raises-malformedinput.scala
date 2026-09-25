// EXPECT-ERROR: MalformedInputException: _data/malformed-utf8.bin: malformed UTF-8 input at byte 1
// A file that is not text. The bytes are A FF FE B LF: 0xFF can begin no UTF-8
// sequence. scalac 3.9.0 raises java.nio.charset.MalformedInputException, and so
// does this -- with the path and the offset in the message, where Scala's says
// only "Input length = 1" (D98).
//
// Decoding this to replacement characters instead would be the worst possible
// outcome: the program would read a corrupted file and never know.
println(Source.fromFile("_data/malformed-utf8.bin").mkString)
