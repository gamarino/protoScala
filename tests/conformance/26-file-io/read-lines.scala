// EXPECT: alpha|beta|gamma
// `Source.fromFile(path).getLines()` -- the reading surface a Scala programmer
// already knows. Verified against scalac 3.9.0: three lines, the terminators
// stripped, and the file's own trailing newline adding no fourth empty line.
println(Source.fromFile("_data/three.txt").getLines().mkString("|"))
