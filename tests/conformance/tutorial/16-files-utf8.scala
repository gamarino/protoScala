// EXPECT: 6 café €
val text = "café €"
FileIO.write("utf8.txt", text)
val back = Source.fromFile("utf8.txt").mkString
// Text is written as UTF-8 and read back as UTF-8, so a length is a count of
// characters and not of bytes: 6 characters in 9 bytes.
println(s"${back.length} $back")
