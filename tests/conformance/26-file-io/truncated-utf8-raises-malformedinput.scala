// EXPECT-ERROR: MalformedInputException: _data/truncated-utf8.bin: malformed UTF-8 input at byte 0
// The other half of the same failure: a file whose LAST sequence is cut short.
// The single byte 0xC3 begins a two-byte sequence that the file then ends
// before. scalac 3.9.0 raises MalformedInputException here too.
println(Source.fromFile("_data/truncated-utf8.bin").mkString)
